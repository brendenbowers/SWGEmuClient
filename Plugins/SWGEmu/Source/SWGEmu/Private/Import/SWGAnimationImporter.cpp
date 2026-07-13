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
// deliberately — IAnimationDataController::SetBoneTrackKeys has TWO
// overloads, and the float-vector one silently CheckOuterClass()-fails
// (returns false, no warning logged) for every single bone, leaving the
// built AnimSequence with NumberOfSampledKeys=1/SequenceLength=0 (no
// animation at all — the bug behind "idle and walk do nothing"). The
// double-precision overload has no such check and works correctly. See
// Engine/Source/Developer/AnimationDataController/Private/
// AnimDataController.cpp's two SetBoneTrackKeys overloads.

UAnimSequence* FSWGAnimationImporter::BuildAnimSequence(
	const FSWGAnimationData& Animation,
	const FSWGSkeletonData& Skeleton,
	USkeleton* TargetSkeleton,
	const FString& PackagePath)
{
	if (Animation.FrameCount <= 0 || Skeleton.Joints.Num() == 0 || !TargetSkeleton)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationImporter: invalid input for '%s' (FrameCount=%d, Joints=%d, TargetSkeleton=%s)"),
			*PackagePath, Animation.FrameCount, Skeleton.Joints.Num(), TargetSkeleton ? TEXT("valid") : TEXT("null"));
		return nullptr;
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

	const FString AssetName = FPackageName::GetShortName(PackagePath);
	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();

	UAnimSequence* AnimSequence = NewObject<UAnimSequence>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	AnimSequence->SetSkeleton(TargetSkeleton);

	// Rebuilding this same asset repeatedly (e.g. re-running
	// swg.BuildWookieeAnimations after a code fix) reloads whatever was
	// already saved to disk from the PREVIOUS attempt — NewObject with an
	// already-loaded name/outer returns that existing UAnimSequence, whose
	// data model has its "population flag" already set from the prior
	// build. With that flag set, AddBoneCurve/SetBoneTrackKeys report success
	// but the model discards the changes (this is the actual mechanism
	// behind hours of "AddBoneCurve returns true but NumBoneTracks stays 0" —
	// confirmed by finding the equivalent FBX reimport code path in
	// SkeletalMeshEdit.cpp, which wraps its whole populate step in exactly
	// this scope). FReimportScope temporarily clears the flag so a rebuild
	// is treated the same as a first-time populate.
	IAnimationDataModel::FReimportScope ReimportScope(AnimSequence->GetDataModel());

	IAnimationDataController& Controller = AnimSequence->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Import SWG .ans animation")), false);
	Controller.ResetModel(false);

	const int32 FrameRateInt = FMath::Max(1, FMath::RoundToInt(Animation.FrameRate));
	Controller.SetFrameRate(FFrameRate(FrameRateInt, 1), false);
	Controller.SetNumberOfFrames(FFrameNumber(FMath::Max(1, Animation.FrameCount - 1)), false);

	for (const FSWGSkeletonJoint& Joint : Skeleton.Joints)
	{
		const FName BoneName(*Joint.Name);
		if (!Controller.AddBoneCurve(BoneName, false))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationImporter: AddBoneCurve failed for bone '%s' — skipping"), *Joint.Name);
			continue;
		}

		TArray<FVector> ScaleKeys;
		ScaleKeys.Init(FVector::OneVector, Animation.FrameCount);

		// Only the root bone gets a translation curve — every other bone's
		// position is a fixed offset from its parent (rotation/FK-driven
		// only), matching how the source data itself only ever supplies one
		// translation curve (FORM ATRN) for the whole clip, not one per bone.
		TArray<FVector> PosKeys;
		if (Joint.Name.Equals(TEXT("root"), ESearchCase::IgnoreCase) && Animation.RootTranslationDeltas.Num() > 0)
		{
			PosKeys = SWGBuildDenseTranslationTrack(Animation.RootTranslationDeltas, Animation.FrameCount, Joint.BindPoseTranslation);
		}
		else
		{
			PosKeys.Init(Joint.BindPoseTranslation, Animation.FrameCount);
		}

		TArray<FQuat> RotKeys;
		const FSWGAnimationBoneTrack* const* FoundTrack = BoneNameToTrack.Find(Joint.Name.ToLower());
		if (FoundTrack)
		{
			RotKeys = SWGBuildDenseRotationTrack((*FoundTrack)->Keyframes, Animation.FrameCount, Joint.BindPoseRotation);
		}
		else
		{
			RotKeys.Init(Joint.BindPoseRotation, Animation.FrameCount);
		}

		if (!Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys, false))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationImporter: SetBoneTrackKeys failed for bone '%s' (PosKeys=%d RotKeys=%d ScaleKeys=%d)"),
				*Joint.Name, PosKeys.Num(), RotKeys.Num(), ScaleKeys.Num());
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationImporter: after population — NumberOfKeys=%d PlayLength=%.3f"),
		AnimSequence->GetDataModel()->GetNumberOfKeys(), AnimSequence->GetPlayLength());

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

	UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationImporter: built '%s' — %d frame(s) @ %.1ffps, %d bone track(s) (%d animated), root translation keys=%d"),
		*PackagePath, Animation.FrameCount, Animation.FrameRate, Skeleton.Joints.Num(), Animation.BoneTracks.Num(), Animation.RootTranslationDeltas.Num());

	return AnimSequence;
}

#endif // WITH_EDITOR
