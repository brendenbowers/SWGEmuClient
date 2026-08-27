#include "Components/SWGMovementComponent.h"
#include "Network/SWGPacket.h"

namespace
{
	// SWG's RunSpeed/WalkSpeed/water-swim fields are in meters/sec; UE is 1
	// unit = 1cm. Position/rotation don't need this — those already arrive in
	// UE-matching world units (see FSWGZoneLoadingState::Enter).
	constexpr float MetersToUnrealUnits = 100.0f;
}

USWGMovementComponent::USWGMovementComponent()
{
	// ASWGPlayer::PossessedBy sets MOVE_Walking before the CREO base4 baseline
	// (RunSpeed/WalkSpeed) arrives; movement at UCharacterMovementComponent's
	// stock defaults would trip Core3's speed-hack check. Start conservative;
	// ApplyBase4 overwrites these once the real values are known.
	MaxWalkSpeed = 155.0f;
	MaxAcceleration = 100.0f;
}

void USWGMovementComponent::ApplyBase4(const FCreatureObjectBaseline& Baseline)
{
	AccelerationMultiplierBase = Baseline.AccelerationMultiplierBase;
	AccelerationMultiplierMod = Baseline.AccelerationMultiplierMod;
	SpeedMultiplierBase = Baseline.SpeedMultiplierBase;
	SpeedMultiplierMod = Baseline.SpeedMultiplierMod;
	RunSpeed = Baseline.RunSpeed;
	SlopeModAngle = Baseline.SlopeModAngle;
	SlopeModPercent = Baseline.SlopeModPercent;
	TurnScale = Baseline.TurnScale;
	WalkSpeed = Baseline.WalkSpeed;
	WaterModPercent = Baseline.WaterModPercent;
	bHasBase4 = true;

	RecomputeMovementLimits();
}

void USWGMovementComponent::ApplyDelta4(const FCreatureObjectDelta& Delta)
{
	if (Delta.AccelerationMultiplierBase.IsSet()) { AccelerationMultiplierBase = *Delta.AccelerationMultiplierBase; }
	if (Delta.AccelerationMultiplierMod.IsSet())  { AccelerationMultiplierMod = *Delta.AccelerationMultiplierMod; }
	if (Delta.SpeedMultiplierBase.IsSet())        { SpeedMultiplierBase = *Delta.SpeedMultiplierBase; }
	if (Delta.SpeedMultiplierMod.IsSet())         { SpeedMultiplierMod = *Delta.SpeedMultiplierMod; }
	if (Delta.RunSpeed.IsSet())                   { RunSpeed = *Delta.RunSpeed; }
	if (Delta.SlopeModAngle.IsSet())              { SlopeModAngle = *Delta.SlopeModAngle; }
	if (Delta.SlopeModPercent.IsSet())            { SlopeModPercent = *Delta.SlopeModPercent; }
	if (Delta.TurnScale.IsSet())                  { TurnScale = *Delta.TurnScale; }
	if (Delta.WalkSpeed.IsSet())                  { WalkSpeed = *Delta.WalkSpeed; }
	if (Delta.WaterModPercent.IsSet())            { WaterModPercent = *Delta.WaterModPercent; }

	RecomputeMovementLimits();
}

void USWGMovementComponent::RecomputeMovementLimits()
{
	// Unreal uses MaxWalkSpeed as the top ground speed for both digital and
	// analog movement. Use SWG's run speed as that ceiling; analog magnitude
	// naturally produces the walk/jog ranges below it, while a full keyboard
	// or stick input reaches the server-provided run speed.
	const float BaselineRunSpeed = RunSpeed * MetersToUnrealUnits;
	const float BaselineWalkSpeed = WalkSpeed * MetersToUnrealUnits;
	MaxWalkSpeed = BaselineRunSpeed > KINDA_SMALL_NUMBER ? BaselineRunSpeed : BaselineWalkSpeed;
	// Same unit conversion as MaxWalkSpeed above — this is missing it would
	// leave MaxAcceleration at ~1 uu/s^2 (the raw multiplier product), so tiny
	// that reaching MaxWalkSpeed from a standstill takes on the order of
	// minutes: the character would visibly only rotate to face movement
	// input (bOrientRotationToMovement doesn't need acceleration) while never
	// actually translating within any reasonable test window.
	const float BaselineAcceleration =
		AccelerationMultiplierBase * AccelerationMultiplierMod * MetersToUnrealUnits;

	// Some Core3 baselines currently send one or both acceleration multipliers
	// as zero. Do not replace the usable constructor default with zero: a
	// CharacterMovementComponent with MaxAcceleration == 0 accepts movement
	// input but can never turn it into velocity.
	if (BaselineAcceleration > KINDA_SMALL_NUMBER)
	{
		MaxAcceleration = BaselineAcceleration;
	}
	RotationRate = FRotator(0.0f, TurnScale, 0.0f);
	// Core3 stores/sends this pre-converted to radians (CreatureObjectImplementation.cpp:
	// "slopeModAngle = (creoData->getSlopeModAngle() * M_PI) / 180.f"), but
	// UCharacterMovementComponent::SetWalkableFloorAngle expects degrees. Passing the
	// raw radians value straight through (e.g. ~0.87 for a 50-degree template default)
	// collapses the walkable threshold to under 1 degree, so almost any surface reads
	// as too steep and the character slides continuously — convert back to degrees.
	//
	// Floored at 55: our baked interior floor/ramp collision is per-triangle
	// complex ("CTF_UseComplexAsSimple") geometry, not the original engine's
	// own floor-graph-based walkability
	SetWalkableFloorAngle(FMath::Max(FMath::RadiansToDegrees(SlopeModAngle), 55.0f));
	MaxSwimSpeed = WaterModPercent * MetersToUnrealUnits;
}
