#pragma once

#include "CoreMinimal.h"

/**
 * Pure maths of retail VehicleHoverDynamics (SWGEmu.exe 0x11ab9c0, see Docs/RetailVehicleRegionNotes.md).
 * Angles are radians, lengths cm, speeds cm/s. Pitch is positive nose up, roll positive right side down.
 */
struct SWGEMUCLIENT_API FSWGHoverParams
{
	float MaxSpeed = 1500.f;
	/** Turn rate (rad/s) at standstill and at MaxSpeed; the turn fraction is measured against the blend. */
	float TurnRateAtRest = 2.f;
	float TurnRateAtMax = 1.f;
	/** Acceleration (cm/s^2) at standstill and at MaxSpeed. */
	float AccelAtRest = 1200.f;
	float AccelAtMax = 400.f;

	/** Nose-up on acceleration / nose-down on braking, at full acceleration (retail 0xe, 0xf: set from vehicle data). */
	float AccelPitch = 0.12f;
	float DecelPitch = 0.12f;
	/** Roll at a full-rate turn at full speed (retail 0x10, a runtime constant not recoverable from the file). */
	float MaxBank = 0.3f;
	/** Clamp on pitch and roll (retail DAT_01998f78, also runtime-initialised). */
	float MaxAngle = 0.6f;

	/** Retail ctor defaults 2, 3, 4, 2.5 (0x28..0x34): response, settle, height and speed-stiffening rates. */
	float ResponseRate = 2.f;
	float SettleRate = 3.f;
	float HeightRate = 4.f;
	float SpeedStiffening = 2.5f;
	/** Retail 0.5: how much terrain pitch/roll is flattened at standstill. */
	float Flattening = 0.5f;
};

struct SWGEMUCLIENT_API FSWGHoverState
{
	FVector2D PreviousForward = FVector2D::ZeroVector;
	float PreviousSpeed = 0.f;
	float PreviousPitchTarget = 0.f;
	float PreviousBankTarget = 0.f;
	float Pitch = 0.f;
	float Roll = 0.f;
	float TerrainPitch = 0.f;
	float TerrainRoll = 0.f;
	float Height = 0.f;
	bool bHasHeight = false;
	/** Signed turn fraction of the last step (retail 0x2f). */
	float TurnFraction = 0.f;
};

struct SWGEMUCLIENT_API FSWGHoverInput
{
	float DeltaSeconds = 0.f;
	float Speed = 0.f;
	/** Unit heading on the ground plane; X forward, Y right. */
	FVector2D Forward = FVector2D(1.0, 0.0);
	/** Ground slope under the vehicle (same sign convention as the output); ignored without ground. */
	float GroundPitch = 0.f;
	float GroundRoll = 0.f;
	float GroundHeight = 0.f;
	bool bHasGround = false;
};

struct SWGEMUCLIENT_API FSWGHoverOutput
{
	float Pitch = 0.f;
	float Roll = 0.f;
	/** Smoothed ground height minus the raw one: the lag that makes the body float over terrain steps. */
	float HeightOffset = 0.f;
	float SpeedFraction = 0.f;
	float TurnFraction = 0.f;
};

struct SWGEMUCLIENT_API FSWGVehicleHoverDynamics
{
	/** Retail 0x11ac440 / 0x11ac4c0: lerp by speed fraction. */
	static float BlendBySpeed(float AtRest, float AtMax, float SpeedFraction);

	/** One simulation step; mutates State. */
	static FSWGHoverOutput Step(const FSWGHoverParams& Params, FSWGHoverState& State, const FSWGHoverInput& Input);
};
