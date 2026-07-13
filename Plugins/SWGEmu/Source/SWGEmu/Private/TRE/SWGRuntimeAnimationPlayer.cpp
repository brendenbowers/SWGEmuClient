#include "TRE/SWGRuntimeAnimationPlayer.h"
#include "Components/PoseableMeshComponent.h"
#include "Common/SWGDenseTrackUtils.h"

// Diagnostic (WOOKIEE_ANIMATION_POSE_BUG.md): while >0, each ApplyPose call
// logs the ankle joints' composed COMPONENT-SPACE positions and decrements.
// Settles "which horizontal axis do the feet actually stride along" with FK
// data instead of visual judgment — component Y matches the mesh's visual
// facing; component X would mean the rotation frame is 90 deg off from it.
static int32 GSWGDebugFootTrackFrames = 0;
static FAutoConsoleVariableRef CVarSWGDebugFootTrack(
	TEXT("swg.DebugFootTrack"), GSWGDebugFootTrackFrames,
	TEXT("Log ankle component-space positions for the next N applied poses (e.g. swg.DebugFootTrack 150)."));

// Live playback-speed multiplier for runtime clips (1 = authored 30fps).
// Diagnostic/tuning aid: slowing the clip makes gait-shape problems visible
// that are ambiguous at full speed.
static float GSWGAnimPlayRate = 1.0f;
static FAutoConsoleVariableRef CVarSWGAnimPlayRate(
	TEXT("swg.AnimPlayRate"), GSWGAnimPlayRate,
	TEXT("Playback speed multiplier for runtime-posed animations (default 1.0; 0.25 = quarter speed). Applies immediately."));

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
			? SWGBuildDenseRotationTrack((*FoundTrack)->Keyframes, Result.FrameCount, Joint.BindPoseRotation)
			: SWGBuildDenseRotationTrack(TMap<int32, FQuat>(), Result.FrameCount, Joint.BindPoseRotation);

		if (Joint.Name.Equals(TEXT("root"), ESearchCase::IgnoreCase) && Animation.RootTranslationDeltas.Num() > 0)
		{
			Result.DenseRootTranslations = SWGBuildDenseTranslationTrack(Animation.RootTranslationDeltas, Result.FrameCount, Joint.BindPoseTranslation);
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

	const float FrameFloat = FMath::Fmod(PlaybackTimeSeconds * GSWGAnimPlayRate * RuntimeAnim.FrameRate, (float)RuntimeAnim.FrameCount);
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
		FQuat MidRotation = FQuat::Slerp(Track.DenseRotations[Frame0], Track.DenseRotations[Frame1], Alpha);

		// Soft-clamp (2026-07-12, replaces the earlier uniform-factor Slerp
		// damping — see WOOKIEE_ANIMATION_POSE_BUG.md) — a UNIFORM
		// Slerp(Target, Mid, factor) scales every frame's swing down by the
		// same fraction, including frames that were already a normal,
		// correctly-shaped part of the gait. That's why the first pass
		// (factor 0.3-0.4 on every frame) produced a visibly shrunken,
		// mincing stride instead of fixing the actual bad frames — user
		// feedback: "legs ... very short" and torso "still unnaturally
		// moving around" even after tightening the factor further. Only
		// clamp frames whose swing genuinely exceeds a plausible max for
		// that joint group; frames already under the cap pass through at
		// full authored amplitude, so a real running stride keeps its full
		// range of motion and only actual outlier keyframes get reined in.
		auto SoftClampSwing = [](const FQuat& Target, FQuat Current, float MaxDegrees) -> FQuat
		{
			FQuat Delta = Target.Inverse() * Current;
			// Quaternion double-cover: Delta and -Delta represent the same
			// rotation, but GetAngle() = 2*acos(W) is NOT hemisphere-aware —
			// if W lands negative it reports the long-way-around angle (up
			// to 360 deg) for what may be a genuinely small rotation. Force
			// the shortest-path hemisphere first or the clamp fraction below
			// comes out wrong on essentially arbitrary frames (whichever
			// hemisphere the quaternion product happened to land in), which
			// reads as sudden per-frame flips/twists — exactly the "legs
			// twist and fly out, head pitching backwards" regression this
			// fixes.
			if (Delta.W < 0.0f)
			{
				Delta = Delta * -1.0f;
			}
			const float DeltaDeg = FMath::RadiansToDegrees(Delta.GetAngle());
			if (DeltaDeg > MaxDegrees && DeltaDeg > KINDA_SMALL_NUMBER)
			{
				const float T = MaxDegrees / DeltaDeg;
				Current = FQuat::Slerp(Target, Current, T);
			}
			return Current;
		};

		// Re-enabled again (2026-07-12) — burst-captured screenshots of the
		// undamped run clip (with the -90 mesh yaw fix in place) showed the
		// character diving so far forward mid-cycle the head left the top
		// of frame. Confirms the ORIGINAL diagnosis (root/spine swing
		// genuinely ~15-60 deg, compounding through a 5-joint chain) was
		// correct all along — the earlier "damping is hiding the issue"
		// feedback was right too, but for a DIFFERENT reason: at the time,
		// damping was hiding a wrong-axis bug, not legitimately taming
		// amplitude. Now that the axis is fixed, damping is back to doing
		// its original, legitimate job. If this still looks wrong, the
		// problem is genuinely in the per-joint amplitude/calibration, not
		// axis orientation.
		// OFF for diagnosis 2026-07-13 (FOOTTRACK stride-axis check on the walk
		// clip) — the user is right that damping can mask a frame error; the
		// clamps must not touch the data while we measure the stride axis.
		constexpr bool bSWGEnablePoseDamping = false;
		if (!bSWGEnablePoseDamping)
		{
			// no-op — Mid passes through unmodified.
		}
		else if (Joint.ParentIndex == INDEX_NONE)
		{
			// Root has no parent to absorb its swing (ComposeLocalRotation's
			// result becomes its ComponentSpace transform directly), so an
			// un-clamped large swing reads as the whole body pitching.
			MidRotation = SoftClampSwing(FQuat::Identity, MidRotation, 20.0f);
		}
		else if (Joint.Name.Contains(TEXT("spine"), ESearchCase::IgnoreCase) || Joint.Name.Contains(TEXT("neck"), ESearchCase::IgnoreCase) || Joint.Name.Equals(TEXT("head"), ESearchCase::IgnoreCase))
		{
			// spine1/spine2/spine3/neck/head form a straight parent chain
			// (root -> spine1 -> spine2 -> spine3 -> neck -> head); even a
			// per-joint-plausible swing compounds multiplicatively across 5
			// joints into a much wider torso/head swing than any single
			// number suggests. Clamp fairly tight per joint since the
			// compounding does the rest. A T-pose's spine/head is already
			// upright, so identity is a safe clamp target (unlike arms).
			MidRotation = SoftClampSwing(FQuat::Identity, MidRotation, 15.0f);
		}
		else if (Joint.Name.Contains(TEXT("thigh"), ESearchCase::IgnoreCase) || Joint.Name.Contains(TEXT("shin"), ESearchCase::IgnoreCase) || Joint.Name.Contains(TEXT("calf"), ESearchCase::IgnoreCase) || Joint.Name.Contains(TEXT("ankle"), ESearchCase::IgnoreCase))
		{
			// Same compounding-chain reasoning as spine (root -> thigh ->
			// shin -> ankle). Legs get a looser cap than spine — a running
			// gait genuinely needs a big thigh/shin swing, unlike the
			// torso — so this only reins in the real outlier keyframes
			// (e.g. the brief ~90 deg direction-reversal spike measured at
			// toe-off in swg.DumpAnsAnimation's rthigh track) instead of
			// shrinking the whole stride uniformly.
			MidRotation = SoftClampSwing(FQuat::Identity, MidRotation, 45.0f);
		}
		else if (RuntimeAnim.RestMidRotations.IsValidIndex(JointIndex) &&
			(Joint.Name.Contains(TEXT("arm"), ESearchCase::IgnoreCase) || Joint.Name.Contains(TEXT("clav"), ESearchCase::IgnoreCase)))
		{
			// Arms are NOT safe to clamp toward FQuat::Identity (see the
			// ruled-out note in WOOKIEE_ANIMATION_POSE_BUG.md — a T-pose's
			// arm bind orientation IS identity Mid, so that relaxes toward
			// arms-out-to-the-sides, which is worse). Clamp toward the idle
			// clip's decoded arm rotation instead — a real "arms resting"
			// reference, populated once at spawn time into
			// RuntimeAnim.RestMidRotations (see USWGMeshGeneratorSubsystem's
			// Wookiee finalize path).
			MidRotation = SoftClampSwing(RuntimeAnim.RestMidRotations[JointIndex], MidRotation, 45.0f);
		}

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

	if (GSWGDebugFootTrackFrames > 0)
	{
		--GSWGDebugFootTrackFrames;

		// One-shot: the BIND pose ankle positions pin down which component
		// axis carries the skeleton's natural hip-width (lateral) separation —
		// the stride axis must be the perpendicular horizontal one.
		// v2: toes included — toe-minus-ankle horizontal offset is the
		// skeleton's true facing sign, immune to l/r naming or mirroring.
		static bool bLoggedBindOnceV2 = false;
		if (!bLoggedBindOnceV2)
		{
			bLoggedBindOnceV2 = true;
			TArray<FTransform> BindTransforms;
			BindTransforms.SetNum(Skeleton.Joints.Num());
			for (int32 JointIndex = 0; JointIndex < Skeleton.Joints.Num(); ++JointIndex)
			{
				const FSWGSkeletonJoint& Joint = Skeleton.Joints[JointIndex];
				const FTransform Local(Joint.ComposeLocalRotation(Joint.BindPoseRotation), Joint.BindPoseTranslation, FVector::OneVector);
				BindTransforms[JointIndex] = Joint.ParentIndex == INDEX_NONE ? Local : Local * BindTransforms[Joint.ParentIndex];
				if (Joint.Name.Equals(TEXT("lankle"), ESearchCase::IgnoreCase) || Joint.Name.Equals(TEXT("rankle"), ESearchCase::IgnoreCase) || Joint.Name.Equals(TEXT("root"), ESearchCase::IgnoreCase)
					|| Joint.Name.Contains(TEXT("toe"), ESearchCase::IgnoreCase) || Joint.Name.Contains(TEXT("head"), ESearchCase::IgnoreCase))
				{
					const FVector B = BindTransforms[JointIndex].GetLocation();
					UE_LOG(LogTemp, Warning, TEXT("FOOTBIND %s comp=(%.2f, %.2f, %.2f)"), *Joint.Name, B.X, B.Y, B.Z);
				}
			}
		}
		for (int32 JointIndex = 0; JointIndex < Skeleton.Joints.Num(); ++JointIndex)
		{
			const FSWGSkeletonJoint& Joint = Skeleton.Joints[JointIndex];
			if (Joint.Name.Equals(TEXT("lankle"), ESearchCase::IgnoreCase) || Joint.Name.Equals(TEXT("rankle"), ESearchCase::IgnoreCase))
			{
				const FVector P = ComponentSpaceTransforms[JointIndex].GetLocation();
				UE_LOG(LogTemp, Warning, TEXT("FOOTTRACK frame=%.2f %s comp=(%.2f, %.2f, %.2f)"),
					WrappedFrameFloat, *Joint.Name, P.X, P.Y, P.Z);
			}
		}
	}
}
