#pragma once

#include "CoreMinimal.h"

struct FSWGSkeletonData;

/**
 * A .sat resolved far enough to animate on any USkeletalMeshComponent: its mesh parts, skeleton and the .lat clips of
 * one logical animation. Built once per .sat by FSWGSkeletalAnimationPipeline::ResolveSatLoopSource.
 */
struct FSWGSatLoopSource
{
	FString SatPath;
	FString SkeletonPath;
	TArray<FString> MeshVirtualPaths;
	FString LatPath;
	FString LogicalName;

	/** The logical animation's clips in .lat order (empty when the .lat has no such entry; the mesh still shows its bind pose). */
	TArray<FString> ClipPaths;

	TSharedPtr<const FSWGSkeletonData> Skeleton;
};
