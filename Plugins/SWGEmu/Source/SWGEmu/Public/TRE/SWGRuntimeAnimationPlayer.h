#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGSkeletonReader.h"
#include "TRE/SWGAnimationReader.h"

class UPoseableMeshComponent;

/**
 * One joint's dense (one-sample-per-frame) rotation track, resolved to the
 * skeleton's own joint order (index-parallel with FSWGSkeletonData::Joints)
 * so ApplyPose doesn't need a name lookup per bone per tick. Joints the clip
 * doesn't animate get FrameCount copies of their bind pose rotation — same
 * "hold the skeleton's own pose" rule FSWGAnimationImporter used.
 */
struct FSWGRuntimeBoneTrack
{
	// "Mid" rotations in the joint's RPRE/RPST frame (the sampled .ans value,
	// or BPRO for joints the clip doesn't animate) — ApplyPose composes the
	// actual local rotation via FSWGSkeletonJoint::ComposeLocalRotation.
	TArray<FQuat> DenseRotations;
};

/**
 * A .ans clip resampled to dense per-frame data, ready for direct per-tick
 * bone posing via UPoseableMeshComponent::SetBoneTransformByName — no
 * UAnimSequence/IAnimationDataController involved at all. Built once when a
 * clip starts playing (see USWGMeshGeneratorSubsystem's playback list), then
 * cheaply sampled every tick by ApplyPose.
 *
 * Exists because building real UAnimSequence assets via
 * IAnimationDataController silently discards every keyframe in this engine
 * build (AddBoneCurve/SetBoneTrackKeys report success but the resulting
 * model's bone-track array stays empty — confirmed exhaustively: bone name
 * lookup succeeds, Controller.GetModel() and AnimSequence->GetDataModel()
 * are the same object, ResetModel and FReimportScope don't help). Driving
 * bones directly at runtime sidesteps that entirely and also works in
 * packaged (non-editor) builds, unlike the AnimSequence-building path.
 */
struct FSWGRuntimeAnimation
{
	float FrameRate = 30.0f;
	int32 FrameCount = 1;

	/** Index-parallel with the FSWGSkeletonData::Joints this was built against. */
	TArray<FSWGRuntimeBoneTrack> BoneTracks;

	/** Dense per-frame translation for the root joint only (bind pose + delta) — every other joint stays at its constant bind pose translation, same as FSWGAnimationImporter. Empty if the clip has no root translation curve. */
	TArray<FVector> DenseRootTranslations;

	/**
	 * Optional per-joint "rest" Mid rotation (index-parallel with Joints),
	 * used only as ApplyPose's damping target for limb joints (arms) where
	 * FQuat::Identity is NOT a usable rest reference — unlike root/spine/legs,
	 * a T-pose's arm bind orientation is held out to the side, not resting at
	 * the body. Populated from a separately-decoded idle clip's frame-0 Mid
	 * rotations (idle is confirmed to render arms-at-sides correctly — see
	 * WOOKIEE_ANIMATION_POSE_BUG.md), left empty to fall back to no arm
	 * damping.
	 */
	TArray<FQuat> RestMidRotations;
};

class SWGEMU_API FSWGRuntimeAnimationPlayer
{
public:
	/** Resamples Animation's sparse keyframes (SLERP for rotation, LERP for root translation) against Skeleton's bind pose into dense per-frame arrays. */
	static FSWGRuntimeAnimation BuildRuntimeAnimation(const FSWGAnimationData& Animation, const FSWGSkeletonData& Skeleton);

	/**
	 * Samples RuntimeAnim at PlaybackTimeSeconds (looping) and applies the
	 * result to PoseableMesh via SetBoneTransformByName, one call per joint,
	 * in Skeleton.Joints order (parent-before-child, matching the file's own
	 * hierarchy order, so PoseableMeshComponent's cascading component-space
	 * recompute sees each parent already up to date).
	 */
	static void ApplyPose(UPoseableMeshComponent& PoseableMesh, const FSWGSkeletonData& Skeleton, const FSWGRuntimeAnimation& RuntimeAnim, float PlaybackTimeSeconds);
};
