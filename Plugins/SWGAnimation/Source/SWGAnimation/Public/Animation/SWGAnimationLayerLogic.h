#pragma once

#include "CoreMinimal.h"

/**
 * Pure selection and clamping rules for layered animation, kept free of engine
 * animation types so they can be unit tested. Addresses are SWGEmu.exe 0x...
 * (decoded 2026-10-10, see Docs/RetailAnimationNotes.md).
 */
namespace SWGAnimLayer
{
	/** DRAT "DIR " codes: a bitfield of right=1, left=2, forward=4, backward=8 (names from the exe's table at 0x15e5b40). */
	namespace Direction
	{
		constexpr uint8 Right = 1;
		constexpr uint8 Left = 2;
		constexpr uint8 Forward = 4;
		constexpr uint8 ForwardRight = 5;
		constexpr uint8 ForwardLeft = 6;
		constexpr uint8 Backward = 8;
		constexpr uint8 BackwardRight = 9;
		constexpr uint8 BackwardLeft = 10;
	}

	/** Heading of a direction code in degrees clockwise from forward: forward 0, right +90, backward 180, left -90. -1000 for an unknown code. */
	SWGANIMATION_API float DirectionAngleDegrees(uint8 DirectionCode);

	/**
	 * The authored direction nearest to the movement heading. Velocity is in the
	 * creature's frame (forward, right). Falls back to Forward, or the first
	 * available code, when Available lacks it. Standing still (below
	 * StillSpeed) keeps PreviousDirection.
	 */
	SWGANIMATION_API uint8 SelectDirection(float ForwardSpeed, float RightSpeed, const TArray<uint8>& Available, uint8 PreviousDirection, float StillSpeed = 5.0f);

	/** YWAT branch: NONE when not turning, YNEG for a left turn (negative yaw rate), YPOS for a right turn. */
	enum class EYawVariant : uint8 { None, Negative, Positive };

	SWGANIMATION_API EYawVariant SelectYawVariant(float YawRateDegreesPerSecond, float ThresholdDegreesPerSecond = 20.0f);

	/** Playback-rate multiplier from a TSCL wrapper; non-positive scales are treated as 1. */
	SWGANIMATION_API float EffectiveTimeScale(float Scale);

	/** ClientSkeletalAnimation config defaults (SWGEmu.exe 0x8318e0): lookAtYawClampDegrees, maxHeadTurnSpeed, targetPitchClampDegrees. */
	struct FLookAtLimits
	{
		float YawClampDegrees = 70.0f;
		float MaxHeadTurnDegreesPerSecond = 600.0f;
		float PitchClampDegrees = 45.0f;
	};

	/**
	 * LookAtTransformModifier yaw (SWGEmu.exe 0x7d08e0): the target's bearing in the
	 * head's frame, wrapped to the shortest arc from the current head yaw, clamped
	 * to +-YawClamp and rate limited to MaxHeadTurnSpeed * DeltaSeconds. Angles in
	 * radians, positive to the right; returns the new head yaw.
	 */
	SWGANIMATION_API float StepLookAtYaw(float DeltaSeconds, float CurrentYawRadians, float TargetForward, float TargetRight, const FLookAtLimits& Limits);

	/**
	 * TargetPitchTransformModifier pitch (SWGEmu.exe 0x7d8fe0): elevation of the
	 * target above the aiming joint, clamped to +-PitchClamp. Positive looks up.
	 * Radians.
	 */
	SWGANIMATION_API float ComputeTargetPitch(float HorizontalDistance, float TargetHeightAboveJoint, const FLookAtLimits& Limits);

	/** MVAC rule (0x7f6f80): the second action plays while the creature has non-zero velocity, the first otherwise. */
	SWGANIMATION_API bool UseMovingAction(float HorizontalSpeed, float StillSpeed = 5.0f);

	/** Weighted pick for an AGAT idle action table; Roll01 in [0,1). Returns INDEX_NONE for an empty or zero-weight table. */
	SWGANIMATION_API int32 PickWeighted(const TArray<float>& Weights, float Roll01);

	/** Blend weight of a timed overlay: ramps in over BlendIn, holds, ramps out over BlendOut ending at Length. */
	SWGANIMATION_API float OverlayWeight(float Elapsed, float Length, float BlendIn, float BlendOut);
}
