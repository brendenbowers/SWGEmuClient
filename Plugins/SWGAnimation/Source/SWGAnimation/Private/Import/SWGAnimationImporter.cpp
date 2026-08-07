#include "Import/SWGAnimationImporter.h"

#if WITH_EDITOR

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Common/SWGDenseTrackUtils.h"

// Dense arrays use double-precision FVector/FQuat (not FVector3f/FQuat4f)
// deliberately — IAnimationDataController::SetBoneTrackKeys's float-vector
// overload silently fails CheckOuterClass() for every bone, leaving the built
// AnimSequence with no animation. The double-precision overload works correctly.

bool FSWGAnimationImporter::BuildAnimSequenceData(
	const FSWGAnimationData& Animation,
	const FSWGSkeletonData& Skeleton,
	FSWGAnimSequenceBuildData& OutData)
{
	if (Animation.FrameCount <= 0 || Skeleton.Joints.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationImporter: invalid input for anim sequence data (FrameCount=%d, Joints=%d)"),
			Animation.FrameCount, Skeleton.Joints.Num());
		return false;
	}

	// Case-insensitive lookup, same convention as FSWGSkeletalMeshImporter —
	// animation bone names (from XFIN) are lowercase, skeleton joint names
	// are mixed case.
	TMap<FString, const FSWGAnimationBoneTrack*> BoneNameToTrack;
	BoneNameToTrack.Reserve(Animation.BoneTracks.Num());
	for (const FSWGAnimationBoneTrack& Track : Animation.BoneTracks)
	{
		BoneNameToTrack.Add(Track.BoneName.ToLower(), &Track);
	}

	OutData.FrameRate = FMath::Max(1, FMath::RoundToInt(Animation.FrameRate));
	OutData.FrameCount = Animation.FrameCount;
	OutData.BoneTracks.Reserve(Skeleton.Joints.Num());

	for (const FSWGSkeletonJoint& Joint : Skeleton.Joints)
	{
		FSWGAnimBoneTrackData& TrackData = OutData.BoneTracks.AddDefaulted_GetRef();
		TrackData.BoneName = FName(*Joint.Name);

		TrackData.ScaleKeys.Init(FVector::OneVector, Animation.FrameCount);

		// Only the root bone gets a translation curve — every other bone's
		// position is a fixed offset from its parent (rotation/FK-driven
		// only), matching how the source data itself only ever supplies one
		// translation curve (FORM ATRN) for the whole clip, not one per bone.
		if (Joint.Name.Equals(TEXT("root"), ESearchCase::IgnoreCase) && Animation.RootTranslationDeltas.Num() > 0)
		{
			TrackData.PosKeys = SWGBuildDenseTranslationTrack(Animation.RootTranslationDeltas, Animation.FrameCount, Joint.BindPoseTranslation);
		}
		else
		{
			TrackData.PosKeys.Init(Joint.BindPoseTranslation, Animation.FrameCount);
		}

		// Each decoded sample is only part of the joint's mid rotation — the
		// actual local rotation is the full Pre/Post composition (see
		// FSWGSkeletonJoint::ComposeLocalRotation), same as
		// FSWGSkeletalMeshImporter composes for the bind pose.
		const FSWGAnimationBoneTrack* const* FoundTrack = BoneNameToTrack.Find(Joint.Name.ToLower());
		if (FoundTrack)
		{
			TrackData.RotKeys = SWGBuildDenseRotationTrack((*FoundTrack)->Keyframes, Animation.FrameCount, Joint.BindPoseRotation);
			for (FQuat& Rot : TrackData.RotKeys)
			{
				// An animated frame's mid is the sample combined with the
				// joint's bind rotation, not the sample alone.
				Rot = Joint.ComposeLocalRotation(Rot * Joint.BindPoseRotation);
			}
		}
		else
		{
			TrackData.RotKeys.Init(Joint.ComposeLocalRotation(Joint.BindPoseRotation), Animation.FrameCount);
		}
	}

	return true;
}

UAnimSequence* FSWGAnimationImporter::FinalizeAnimSequence(
	FSWGAnimSequenceBuildData&& Data,
	USkeleton* TargetSkeleton,
	const FString& PackagePath)
{
	if (!TargetSkeleton)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationImporter: no TargetSkeleton for '%s'"), *PackagePath);
		return nullptr;
	}

	const FString AssetName = FPackageName::GetShortName(PackagePath);
	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();

	UAnimSequence* AnimSequence = NewObject<UAnimSequence>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	AnimSequence->SetSkeleton(TargetSkeleton);

	// Rebuilding this same asset repeatedly reloads the previous attempt's
	// UAnimSequence, whose data model already has its "population flag" set —
	// with that set, AddBoneCurve/SetBoneTrackKeys report success but discard
	// the changes. FReimportScope clears the flag so a rebuild populates fresh.
	IAnimationDataModel::FReimportScope ReimportScope(AnimSequence->GetDataModel());

	IAnimationDataController& Controller = AnimSequence->GetController();
	// GetController() only calls SetModel() the first time it lazily creates the
	// controller; without this explicit call (and InitializeModel()) here,
	// AddBoneCurve/SetBoneTrackKeys report success but the writes never reach
	// the model — every generated sequence ends up with zero tracks.
	Controller.SetModel(AnimSequence->GetDataModelInterface());
	Controller.OpenBracket(FText::FromString(TEXT("Import SWG .ans animation")), false);
	Controller.InitializeModel();
	Controller.ResetModel(false);

	Controller.SetFrameRate(FFrameRate(Data.FrameRate, 1), false);
	Controller.SetNumberOfFrames(FFrameNumber(FMath::Max(1, Data.FrameCount - 1)), false);

	for (const FSWGAnimBoneTrackData& TrackData : Data.BoneTracks)
	{
		if (!Controller.AddBoneCurve(TrackData.BoneName, false))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationImporter: AddBoneCurve failed for bone '%s' — skipping"), *TrackData.BoneName.ToString());
			continue;
		}

		if (!Controller.SetBoneTrackKeys(TrackData.BoneName, TrackData.PosKeys, TrackData.RotKeys, TrackData.ScaleKeys, false))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationImporter: SetBoneTrackKeys failed for bone '%s' (PosKeys=%d RotKeys=%d ScaleKeys=%d)"),
				*TrackData.BoneName.ToString(), TrackData.PosKeys.Num(), TrackData.RotKeys.Num(), TrackData.ScaleKeys.Num());
		}
	}

	Controller.NotifyPopulated();
	Controller.CloseBracket(false);

	AnimSequence->MarkPackageDirty();
	AnimSequence->PostEditChange();

	FAssetRegistryModule::AssetCreated(AnimSequence);

	const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	UPackage::SavePackage(Package, AnimSequence, *FileName, SaveArgs);

	UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationImporter: built '%s' — %d bone track(s), PlayLength=%.3fs"),
		*PackagePath, AnimSequence->GetDataModel()->GetNumBoneTracks(), AnimSequence->GetPlayLength());

	return AnimSequence;
}

#endif // WITH_EDITOR
