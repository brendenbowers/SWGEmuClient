#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGAnimationReader.h"
#include "TRE/SWGSkeletonReader.h"

#if WITH_EDITOR

class UAnimSequence;
class USkeleton;

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
	 * Returns nullptr and logs the reason on failure.
	 */
	static UAnimSequence* BuildAnimSequence(
		const FSWGAnimationData& Animation,
		const FSWGSkeletonData& Skeleton,
		USkeleton* TargetSkeleton,
		const FString& PackagePath);
};

#endif // WITH_EDITOR
