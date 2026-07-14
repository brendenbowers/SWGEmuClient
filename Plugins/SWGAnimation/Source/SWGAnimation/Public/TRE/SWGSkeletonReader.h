#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/** One joint's bind-pose transform and hierarchy position. */
struct FSWGSkeletonJoint
{
	FString Name;
	int32 ParentIndex = -1; // -1 for the root joint

	// Already converted to UE's Z-up, 100-units-per-meter convention (see
	// SWGMeshReader.cpp's ReadPositionVector3LE) — parent-relative, not world.
	FVector BindPoseTranslation = FVector::ZeroVector;
	FQuat BindPoseRotation = FQuat::Identity;

	// Per-joint pre/post rotations (RPRE/RPST chunks). A joint's actual local
	// rotation is always the composition Pre * mid * Post, where "mid" is
	// BindPoseRotation (BPRO) at bind and the .ans sample during playback —
	// matching the original client's
	// preMultiplyRotation * animationRotation * postMultiplyRotation.
	// This includes the bind pose: e.g. all_b.skt stores rThigh's Post as a
	// 180° X flip and rShin's translation as +43 up — the right knee only
	// lands below the hip once the flip is composed in. (SWG mirrors the
	// right side and reorients wrists/fingers via these.) The reference
	// skeleton (FSWGSkeletalMeshImporter) and runtime posing
	// (FSWGRuntimeAnimationPlayer) must both compose them, and identically,
	// or skinning breaks against the inverse-bind matrices.
	// Identity for joints the file gives no pre/post rotation.
	FQuat PreRotation = FQuat::Identity;
	FQuat PostRotation = FQuat::Identity;

	/**
	 * The joint's actual parent-relative rotation for a given mid rotation
	 * (bind: BindPoseRotation; animated: the sampled .ans rotation).
	 * Post * mid * Pre — confirmed order: the Y/Z-swap quaternion conversion
	 * (SkeletonReadRotationLE and the .ans decoders) reverses quaternion
	 * multiplication order, so the original client's RPRE * mid * RPST maps
	 * to this reversed order in UE space.
	 */
	FQuat ComposeLocalRotation(const FQuat& Mid) const
	{
		return PostRotation * Mid * PreRotation;
	}
};

struct FSWGSkeletonData
{
	TArray<FSWGSkeletonJoint> Joints; // parent-before-child order, as stored in the file
};

/**
 * Parses SWG's .skt (FORM SLOD) skeleton format — bone hierarchy + bind pose
 * only, read directly from TRE bytes at runtime, same as FSWGMeshReader. See
 * world-object-plan.html for the .msh/.mgn format writeups this follows the
 * same reverse-engineering approach as; .skt itself isn't documented there
 * yet (found via TreTool2.ps1 ifftree against appearance/skeleton/all_b.skt).
 *
 * FORM SLOD wraps multiple FORM SKTM children, one per skeleton LOD level
 * (most-detailed/highest-joint-count first) — only the first (most detailed)
 * is read; there's no current need for the coarser LODs.
 */
class SWGANIMATION_API FSWGSkeletonReader
{
public:
	/** Parses a .skt buffer (FORM SLOD), taking the most detailed LOD. Returns false if unrecognized. */
	static bool ReadSkeleton(const FSWGIffReader& Reader, FSWGSkeletonData& OutSkeleton);

private:
	/** Finds the first FORM child with the given FormType among Parent's direct children. */
	static bool FindChildForm(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& FormType, FSWGIffChunk& OutChunk);

	/** Finds the first leaf chunk with the given Tag among Parent's direct children. */
	static bool FindChildChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& Tag, FSWGIffChunk& OutChunk);

	/** All FORM children (any FormType) among Parent's direct children. */
	static TArray<FSWGIffChunk> FindChildForms(const FSWGIffReader& Reader, const FSWGIffChunk& Parent);

	/** Splits a chunk of back-to-back null-terminated strings (NAME) into Count individual names. */
	static TArray<FString> ReadNullTerminatedStrings(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk, int32 Count);
};
