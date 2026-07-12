#include "TRE/SWGRuntimeAnimationPlayer.h"
#include "Components/PoseableMeshComponent.h"

namespace
{
	// Same sparse (frame -> value) -> dense (one sample per frame) resampling
	// FSWGAnimationImporter used for building UAnimSequence tracks, just
	// producing plain arrays for direct runtime sampling instead of feeding
	// IAnimationDataController.
	TArray<FQuat> BuildDenseRotationTrack(const TMap<int32, FQuat>& Sparse, int32 FrameCount, const FQuat& Fallback)
	{
		TArray<FQuat> Dense;
		Dense.SetNum(FrameCount);

		if (Sparse.Num() == 0)
		{
			for (int32 i = 0; i < FrameCount; ++i)
			{
				Dense[i] = Fallback;
			}
			return Dense;
		}

		TArray<int32> KnownFrames;
		Sparse.GetKeys(KnownFrames);
		KnownFrames.Sort();

		for (int32 Frame = 0; Frame < FrameCount; ++Frame)
		{
			if (const FQuat* Exact = Sparse.Find(Frame))
			{
				Dense[Frame] = *Exact;
				continue;
			}

			int32 PrevFrame = INDEX_NONE, NextFrame = INDEX_NONE;
			for (int32 KnownFrame : KnownFrames)
			{
				if (KnownFrame < Frame) PrevFrame = KnownFrame;
				if (KnownFrame > Frame && NextFrame == INDEX_NONE) NextFrame = KnownFrame;
			}

			if (PrevFrame == INDEX_NONE)
			{
				Dense[Frame] = Sparse[NextFrame];
			}
			else if (NextFrame == INDEX_NONE)
			{
				Dense[Frame] = Sparse[PrevFrame];
			}
			else
			{
				const float Alpha = (float)(Frame - PrevFrame) / (float)(NextFrame - PrevFrame);
				Dense[Frame] = FQuat::Slerp(Sparse[PrevFrame], Sparse[NextFrame], Alpha);
			}
		}
		return Dense;
	}

	TArray<FVector> BuildDenseTranslationTrack(const TMap<int32, FVector>& Sparse, int32 FrameCount, const FVector& BindPose)
	{
		TArray<FVector> Dense;
		Dense.SetNum(FrameCount);

		if (Sparse.Num() == 0)
		{
			for (int32 i = 0; i < FrameCount; ++i)
			{
				Dense[i] = BindPose;
			}
			return Dense;
		}

		TArray<int32> KnownFrames;
		Sparse.GetKeys(KnownFrames);
		KnownFrames.Sort();

		for (int32 Frame = 0; Frame < FrameCount; ++Frame)
		{
			FVector Delta;
			if (const FVector* Exact = Sparse.Find(Frame))
			{
				Delta = *Exact;
			}
			else
			{
				int32 PrevFrame = INDEX_NONE, NextFrame = INDEX_NONE;
				for (int32 KnownFrame : KnownFrames)
				{
					if (KnownFrame < Frame) PrevFrame = KnownFrame;
					if (KnownFrame > Frame && NextFrame == INDEX_NONE) NextFrame = KnownFrame;
				}

				if (PrevFrame == INDEX_NONE)
				{
					Delta = Sparse[NextFrame];
				}
				else if (NextFrame == INDEX_NONE)
				{
					Delta = Sparse[PrevFrame];
				}
				else
				{
					const float Alpha = (float)(Frame - PrevFrame) / (float)(NextFrame - PrevFrame);
					Delta = FMath::Lerp(Sparse[PrevFrame], Sparse[NextFrame], Alpha);
				}
			}
			Dense[Frame] = BindPose + Delta;
		}
		return Dense;
	}
}

FSWGRuntimeAnimation FSWGRuntimeAnimationPlayer::BuildRuntimeAnimation(const FSWGAnimationData& Animation, const FSWGSkeletonData& Skeleton)
{
	FSWGRuntimeAnimation Result;
	Result.FrameRate = Animation.FrameRate;
	Result.FrameCount = FMath::Max(1, Animation.FrameCount);

	TMap<FString, const FSWGAnimationBoneTrack*> BoneNameToTrack;
	BoneNameToTrack.Reserve(Animation.BoneTracks.Num());
	for (const FSWGAnimationBoneTrack& Track : Animation.BoneTracks)
	{
		BoneNameToTrack.Add(Track.BoneName.ToLower(), &Track);
	}

	Result.BoneTracks.SetNum(Skeleton.Joints.Num());
	for (int32 JointIndex = 0; JointIndex < Skeleton.Joints.Num(); ++JointIndex)
	{
		const FSWGSkeletonJoint& Joint = Skeleton.Joints[JointIndex];
		const FSWGAnimationBoneTrack* const* FoundTrack = BoneNameToTrack.Find(Joint.Name.ToLower());

		// Dense tracks hold "mid" rotations in the joint's RPRE/RPST frame —
		// the raw .ans sample, or BPRO (the bind-pose mid value) for joints
		// the clip doesn't animate. ApplyPose composes the actual local
		// rotation (Pre/Post around the mid) per frame, so both cases go
		// through identical math.
		Result.BoneTracks[JointIndex].DenseRotations = FoundTrack
			? BuildDenseRotationTrack((*FoundTrack)->Keyframes, Result.FrameCount, Joint.BindPoseRotation)
			: BuildDenseRotationTrack(TMap<int32, FQuat>(), Result.FrameCount, Joint.BindPoseRotation);

		if (Joint.Name.Equals(TEXT("root"), ESearchCase::IgnoreCase) && Animation.RootTranslationDeltas.Num() > 0)
		{
			Result.DenseRootTranslations = BuildDenseTranslationTrack(Animation.RootTranslationDeltas, Result.FrameCount, Joint.BindPoseTranslation);
		}
	}

	return Result;
}

void FSWGRuntimeAnimationPlayer::ApplyPose(UPoseableMeshComponent& PoseableMesh, const FSWGSkeletonData& Skeleton, const FSWGRuntimeAnimation& RuntimeAnim, float PlaybackTimeSeconds)
{
	if (RuntimeAnim.FrameCount <= 0 || RuntimeAnim.BoneTracks.Num() != Skeleton.Joints.Num())
	{
		return;
	}

	const float FrameFloat = FMath::Fmod(PlaybackTimeSeconds * RuntimeAnim.FrameRate, (float)RuntimeAnim.FrameCount);
	const float WrappedFrameFloat = FrameFloat < 0.0f ? FrameFloat + RuntimeAnim.FrameCount : FrameFloat;
	const int32 Frame0 = FMath::Clamp(FMath::FloorToInt(WrappedFrameFloat), 0, RuntimeAnim.FrameCount - 1);
	const int32 Frame1 = (Frame0 + 1) % RuntimeAnim.FrameCount;
	const float Alpha = WrappedFrameFloat - Frame0;

	// EBoneSpaces has no "relative to parent" option (its LocalSpace enumerator
	// is commented out in the engine) — only WorldSpace and ComponentSpace —
	// so each joint's parent-relative local transform has to be composed into
	// component space ourselves, walking the hierarchy top-down. Skeleton's
	// own joints are stored parent-before-child (see FSWGSkeletonData's
	// comment), so each joint's parent is always already computed by the
	// time it's this joint's turn.
	TArray<FTransform> ComponentSpaceTransforms;
	ComponentSpaceTransforms.SetNum(Skeleton.Joints.Num());

	for (int32 JointIndex = 0; JointIndex < Skeleton.Joints.Num(); ++JointIndex)
	{
		const FSWGSkeletonJoint& Joint = Skeleton.Joints[JointIndex];
		const FSWGRuntimeBoneTrack& Track = RuntimeAnim.BoneTracks[JointIndex];

		// Every joint's actual local rotation is the RPRE/RPST composition
		// around a "mid" rotation — BPRO at bind, the sampled .ans value when
		// animated (see FSWGSkeletonJoint::ComposeLocalRotation). Un-animated
		// joints' dense tracks hold BPRO, so they compose to their bind local
		// the same way. Must match FSWGSkeletalMeshImporter's reference
		// skeleton exactly, or the pose fights the inverse-bind matrices.
		const FQuat MidRotation = FQuat::Slerp(Track.DenseRotations[Frame0], Track.DenseRotations[Frame1], Alpha);
		const FQuat Rotation = Joint.ComposeLocalRotation(MidRotation);

		FVector Translation = Joint.BindPoseTranslation;
		if (Joint.ParentIndex == INDEX_NONE && RuntimeAnim.DenseRootTranslations.Num() == RuntimeAnim.FrameCount)
		{
			Translation = FMath::Lerp(RuntimeAnim.DenseRootTranslations[Frame0], RuntimeAnim.DenseRootTranslations[Frame1], Alpha);
		}

		const FTransform LocalTransform(Rotation, Translation, FVector::OneVector);
		const FTransform ComponentTransform = Joint.ParentIndex == INDEX_NONE
			? LocalTransform
			: LocalTransform * ComponentSpaceTransforms[Joint.ParentIndex];

		ComponentSpaceTransforms[JointIndex] = ComponentTransform;
		PoseableMesh.SetBoneTransformByName(FName(*Joint.Name), ComponentTransform, EBoneSpaces::ComponentSpace);
	}
}
