#include "Components/SWGMovementComponent.h"
#include "Common/SWGMovementTables.h"
// CharacterMovementComponent.h only forward-declares ACharacter.
#include "GameFramework/Character.h"
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

void USWGMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (NetworkTargetLocation.IsSet() && CharacterOwner && DeltaTime > KINDA_SMALL_NUMBER)
	{
		TickNetworkSmoothing(DeltaTime);
		return;
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void USWGMovementComponent::TickNetworkSmoothing(float DeltaTime)
{
	const FVector Current = CharacterOwner->GetActorLocation();
	const FVector Target = *NetworkTargetLocation;
	const FVector ToTarget = Target - Current;
	const float HorizontalDistance = ToTarget.Size2D();

	// Close enough: settle on the target and stop, so a creature that has
	// finished moving idles rather than creeping the last fraction of a unit.
	if (HorizontalDistance <= NetworkArrivalTolerance)
	{
		CharacterOwner->SetActorLocation(Target);
		Velocity = FVector::ZeroVector;
		ClearNetworkTarget();
		return;
	}

	// Spread the gap over one update interval, so motion stays continuous
	// instead of stop-start.
	float Speed = HorizontalDistance / NetworkSmoothingTime;

	// Cap at the posture-scaled run speed (plus catch-up headroom) so nothing
	// outruns its animation — but only once that speed is known: a creature
	// whose CREO base4 hasn't arrived reports zero, and capping to that would
	// leave it crawling. The rate above is self-limiting, so uncapped is the
	// safe direction to fail.
	const float PostureRunSpeed = GetPostureRunSpeed();
	if (PostureRunSpeed > KINDA_SMALL_NUMBER)
	{
		Speed = FMath::Min(Speed, PostureRunSpeed * NetworkCatchUpSpeedTolerance);
	}

	const float Step = FMath::Min(Speed * DeltaTime, HorizontalDistance);

	const FVector HorizontalDirection = FVector(ToTarget.X, ToTarget.Y, 0.0f).GetSafeNormal();
	FVector NewLocation = Current + HorizontalDirection * Step;

	// Height tracks on its own clock: terrain can rise faster than the creature
	// walks, and easing Z at the horizontal rate leaves it wading through slopes.
	NewLocation.Z = FMath::FInterpTo(Current.Z, Target.Z, DeltaTime, NetworkHeightInterpSpeed);
	CharacterOwner->SetActorLocation(NewLocation);

	// The blend space reads Velocity to pick walk vs. run; deriving it from the
	// step actually taken keeps the animation honest about the speed travelled.
	Velocity = HorizontalDirection * (Step / DeltaTime);

	if (NetworkTargetYaw.IsSet())
	{
		// RInterpTo rather than interpolating the yaw scalar: it normalizes the
		// delta, so a heading crossing the 0/360 seam turns the short way.
		const FRotator Rotation = CharacterOwner->GetActorRotation();
		const FRotator TargetRotation(Rotation.Pitch, *NetworkTargetYaw, Rotation.Roll);
		CharacterOwner->SetActorRotation(FMath::RInterpTo(Rotation, TargetRotation, DeltaTime, NetworkYawInterpSpeed));
	}
}

void USWGMovementComponent::ApplyPostureAndStates(ESWGPosture NewPosture, int64 NewStateBitmask)
{
	Posture = NewPosture;
	StateBitmask = NewStateBitmask;

	RecomputeMovementLimits();
}

float USWGMovementComponent::GetPostureWalkSpeed() const
{
	const FSWGPostureMovement* Row = SWGMovementTables::FindPosture(Posture);
	const float Scale = Row ? Row->MovementScale : 1.0f;
	return WalkSpeed * MetersToUnrealUnits * Scale * SWGMovementTables::GetStateRateModifier(StateBitmask);
}

float USWGMovementComponent::GetPostureRunSpeed() const
{
	const FSWGPostureMovement* Row = SWGMovementTables::FindPosture(Posture);
	const float Scale = Row ? Row->MovementScale : 1.0f;
	return RunSpeed * MetersToUnrealUnits * Scale * SWGMovementTables::GetStateRateModifier(StateBitmask);
}

ESWGLocomotion USWGMovementComponent::GetCurrentLocomotion() const
{
	const float Speed = Velocity.Size2D();
	ESWGSpeedCategory Category = SWGMovementTables::ClassifySpeed(Speed, GetPostureWalkSpeed(), GetPostureRunSpeed());

	// A state ceiling can only lower the bucket — Aiming still walks, but a
	// creature the server has Immobilized reads as stationary even if the
	// last position delta hasn't caught up yet.
	Category = FMath::Min(Category, SWGMovementTables::GetMaxSpeedCategory(StateBitmask));

	return SWGMovementTables::ResolveLocomotion(Posture, Category);
}

void USWGMovementComponent::SetTemplateTurnRates(float RunTurnRate, float WalkTurnRate)
{
	TemplateRunTurnRate = RunTurnRate;
	TemplateWalkTurnRate = WalkTurnRate;
	RecomputeMovementLimits();
}

void USWGMovementComponent::RecomputeMovementLimits()
{
	// Posture/state changes can land before base4 (base3 precedes it in the
	// baseline sequence), and deriving MaxWalkSpeed from still-zero speeds
	// leaves the creature unable to move at all. ApplyBase4 calls this again
	// with the posture cached by then, so nothing is lost by skipping early.
	if (!bHasBase4)
	{
		return;
	}

	// Unreal uses MaxWalkSpeed as the top ground speed for both digital and
	// analog movement. Use SWG's run speed as that ceiling; analog magnitude
	// naturally produces the walk/jog ranges below it, while a full keyboard
	// or stick input reaches the server-provided run speed.
	// Posture and state shaping, from datatables/movement — see SWGMovementTables.
	// A missing row (unknown posture, or the tables failed to load) leaves the
	// scales neutral so movement degrades to the raw CREO values rather than
	// stopping.
	const FSWGPostureMovement* PostureRow = SWGMovementTables::FindPosture(Posture);
	const float PostureMovementScale = PostureRow ? PostureRow->MovementScale : 1.0f;
	const float PostureAccelerationScale = PostureRow ? PostureRow->AccelerationScale : 1.0f;
	const float PostureTurnScale = PostureRow ? PostureRow->TurnScale : 1.0f;
	const float StateRateModifier = SWGMovementTables::GetStateRateModifier(StateBitmask);
	const float SpeedScale = PostureMovementScale * StateRateModifier;

	const float BaselineRunSpeed = RunSpeed * MetersToUnrealUnits * SpeedScale;
	const float BaselineWalkSpeed = WalkSpeed * MetersToUnrealUnits * SpeedScale;

	// The state ceiling picks *which* of the two speeds is the cap, rather than
	// scaling either: movementstates.iff is a category ("this state can't move
	// faster than slow"), and slow is exactly the walk speed.
	const ESWGSpeedCategory SpeedCategory = SWGMovementTables::GetMaxSpeedCategory(StateBitmask);
	switch (SpeedCategory)
	{
		case ESWGSpeedCategory::Stationary:
			MaxWalkSpeed = 0.0f;
			break;
		case ESWGSpeedCategory::Slow:
			MaxWalkSpeed = BaselineWalkSpeed;
			break;
		default:
			MaxWalkSpeed = BaselineRunSpeed > KINDA_SMALL_NUMBER ? BaselineRunSpeed : BaselineWalkSpeed;
			break;
	}
	// Same unit conversion as MaxWalkSpeed above — this is missing it would
	// leave MaxAcceleration at ~1 uu/s^2 (the raw multiplier product), so tiny
	// that reaching MaxWalkSpeed from a standstill takes on the order of
	// minutes: the character would visibly only rotate to face movement
	// input (bOrientRotationToMovement doesn't need acceleration) while never
	// actually translating within any reasonable test window.
	const float BaselineAcceleration =
		AccelerationMultiplierBase * AccelerationMultiplierMod * MetersToUnrealUnits * PostureAccelerationScale;

	// Some Core3 baselines currently send one or both acceleration multipliers
	// as zero. Do not replace the usable constructor default with zero: a
	// CharacterMovementComponent with MaxAcceleration == 0 accepts movement
	// input but can never turn it into velocity.
	if (BaselineAcceleration > KINDA_SMALL_NUMBER)
	{
		MaxAcceleration = BaselineAcceleration;
	}
	// TurnScale is a multiplier (Core3 defaults it to 1.0), not degrees/second —
	// on its own it turned every creature at 1°/s.
	const float TemplateTurnRate = SpeedCategory == ESWGSpeedCategory::Slow ? TemplateWalkTurnRate : TemplateRunTurnRate;
	RotationRate = FRotator(0.0f, TemplateTurnRate * TurnScale * PostureTurnScale, 0.0f);
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
