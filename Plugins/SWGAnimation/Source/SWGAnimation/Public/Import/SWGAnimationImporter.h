#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGAnimationReader.h"
#include "TRE/SWGSkeletonReader.h"

#if WITH_EDITOR

class UAnimSequence;
class USkeleton;

/** One bone's already-composed dense pos/rot/scale key arrays (one sample
 *  per frame, rotation already composed with the joint's Pre/Post + bind
 *  pose — see FSWGAnimationImporter::BuildAnimSequenceData) — plain data,
 *  safe to build on a worker thread. */
struct FSWGAnimBoneTrackData
{
	FName BoneName;
	TArray<FVector> PosKeys;
	TArray<FQuat> RotKeys;
	TArray<FVector> ScaleKeys;
};

/** Everything FSWGAnimationImporter::BuildAnimSequenceData produces — plain
 *  data only, safe to build on a worker thread and hand over to
 *  FSWGAnimationImporter::FinalizeAnimSequence on the game thread. */
struct FSWGAnimSequenceBuildData
{
	TArray<FSWGAnimBoneTrackData> BoneTracks;
	int32 FrameRate = 30;
	int32 FrameCount = 1;
};

/**
 * Builds a real, playable UAnimSequence from already-parsed .ans data
 * (FSWGAnimationReader) bound to a skeleton built by FSWGSkeletalMeshImporter
 * — same editor-only reasoning/APIs (IAnimationDataController), see
 * SWGEmu.Build.cs's WITH_EDITOR-guarded dependencies.
 */
class SWGANIMATION_API FSWGAnimationImporter
{
public:
	/**
	 * Bones the clip doesn't animate (not present in Animation.BoneTracks)
	 * keep Skeleton's own bind pose rotation for the whole clip. Only the
	 * root bone gets a translation curve (Animation.RootTranslationDeltas,
	 * added on top of its bind pose translation) — every other bone's
	 * position stays constant at its bind pose value, since the source data
	 * itself only ever supplies one translation curve per clip, not one per
	 * bone (see FSWGAnimationReader.h). Scale is always constant (1,1,1).
	 *
	 * Sparse per-frame keys (both rotation and root translation) are
	 * converted to dense per-frame samples via SLERP/LERP interpolation,
	 * since UAnimSequence's bone track API expects one sample per frame, not
	 * sparse/keyed data.
	 *
	 * Worker-thread-safe half of the split: converts sparse .ans keyframes
	 *  into dense, bind-pose-composed per-bone tracks (the actual SLERP/LERP
	 *  CPU work) — no UObject touched. */
	static bool BuildAnimSequenceData(
		const FSWGAnimationData& Animation,
		const FSWGSkeletonData& Skeleton,
		FSWGAnimSequenceBuildData& OutData);

	/** Game-thread-only half of the split: creates the UAnimSequence and
	 *  pushes the precomputed tracks through IAnimationDataController —
	 *  must run on the game thread (SetBoneTrackKeys broadcasts a
	 *  model-changed notification, the same category of editor-UI-facing
	 *  infrastructure as Slate — see mesh-threading-plan.html's animation
	 *  section for why this half, unlike the mesh builder, has no plain-data
	 *  bypass). */
	static UAnimSequence* FinalizeAnimSequence(
		FSWGAnimSequenceBuildData&& Data,
		USkeleton* TargetSkeleton,
		const FString& PackagePath);
};

#endif // WITH_EDITOR
