#include "Vehicle/SWGVehicleHoverDynamics.h"

float FSWGVehicleHoverDynamics::BlendBySpeed(float AtRest, float AtMax, float SpeedFraction)
{
	return AtRest + (AtMax - AtRest) * FMath::Clamp(SpeedFraction, 0.f, 1.f);
}

FSWGHoverOutput FSWGVehicleHoverDynamics::Step(const FSWGHoverParams& Params, FSWGHoverState& State, const FSWGHoverInput& Input)
{
	FSWGHoverOutput Output;
	if (Input.DeltaSeconds <= UE_SMALL_NUMBER || Params.MaxSpeed <= 0.f)
	{
		Output.Pitch = State.Pitch;
		Output.Roll = State.Roll;
		return Output;
	}

	// First step: no history, so no acceleration yet.
	if (State.PreviousForward.IsNearlyZero())
	{
		State.PreviousSpeed = Input.Speed;
	}

	const float DeltaSeconds = Input.DeltaSeconds;
	const float SpeedFraction = FMath::Clamp(Input.Speed / Params.MaxSpeed, 0.f, 1.f);
	const float ResponseBlend = FMath::Min(DeltaSeconds * Params.ResponseRate, 1.f);
	const float SettleBlend = FMath::Min(DeltaSeconds * Params.SettleRate, 1.f);
	const float SettleBlendFast = FMath::Min(SettleBlend * 2.f, 1.f);
	const float HeightBlend = FMath::Min(DeltaSeconds * Params.HeightRate / (SpeedFraction * Params.SpeedStiffening + 1.f), 1.f);

	// Heading change this step against the turn rate the vehicle can sustain at this speed.
	float TurnFraction = 0.f;
	float TurnSign = 1.f;
	if (!State.PreviousForward.IsNearlyZero() && !Input.Forward.IsNearlyZero())
	{
		const float Cosine = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(State.PreviousForward, Input.Forward)), -1.f, 1.f);
		const float Angle = FMath::Acos(Cosine);
		const float MaxTurnRate = BlendBySpeed(Params.TurnRateAtRest, Params.TurnRateAtMax, SpeedFraction);
		if (MaxTurnRate > 0.f)
		{
			TurnFraction = FMath::Clamp(Angle / DeltaSeconds / MaxTurnRate, 0.f, 1.f);
		}
		TurnSign = FVector2D::CrossProduct(State.PreviousForward, Input.Forward) < 0.0 ? -1.f : 1.f;
	}

	// Acceleration pitch: nose up while speeding up (strongest from rest), nose down while braking (strongest at speed); turning suppresses both.
	float AccelPitchTarget = 0.f;
	const float MaxAccel = BlendBySpeed(Params.AccelAtRest, Params.AccelAtMax, SpeedFraction);
	const float Accel = (Input.Speed - State.PreviousSpeed) / DeltaSeconds;
	if (MaxAccel > 0.f && !FMath::IsNearlyZero(Accel))
	{
		const float AccelFraction = FMath::Clamp(FMath::Abs(Accel) / MaxAccel, 0.f, 1.f);
		AccelPitchTarget = Accel < 0.f
			? -((1.f - TurnFraction) * Params.DecelPitch * AccelFraction * SpeedFraction)
			: (1.f - SpeedFraction) * (1.f - TurnFraction) * Params.AccelPitch * AccelFraction;
	}
	const float SmoothedPitch = (AccelPitchTarget - State.PreviousPitchTarget) * SettleBlendFast + State.PreviousPitchTarget;
	State.PreviousPitchTarget = AccelPitchTarget;

	// Banking leans into the turn; the 0.75/0.25 term keeps a little bank at low speed.
	const float BankTarget = TurnFraction > 0.f ? TurnSign * Params.MaxBank * TurnFraction * (SpeedFraction * 0.75f + 0.25f) : 0.f;
	const float SmoothedBank = (BankTarget - State.PreviousBankTarget) * FMath::Min(ResponseBlend * 2.f, 1.f) + State.PreviousBankTarget;
	State.PreviousBankTarget = BankTarget;

	// Terrain slope, flattened at low speed, then damped (roll settles faster back to level than it builds).
	float TargetTerrainPitch = Input.bHasGround ? Input.GroundPitch : 0.f;
	float TargetTerrainRoll = Input.bHasGround ? Input.GroundRoll : 0.f;
	const float Slow = 1.f - SpeedFraction;
	const float FlattenAmount = Slow * Slow * Slow * Slow * Params.Flattening;
	TargetTerrainPitch -= TargetTerrainPitch * FlattenAmount;
	TargetTerrainRoll -= TargetTerrainRoll * FlattenAmount;
	State.TerrainPitch += (TargetTerrainPitch - State.TerrainPitch) * SettleBlend;
	State.TerrainRoll += (TargetTerrainRoll - State.TerrainRoll) * (FMath::Abs(TargetTerrainRoll) < FMath::Abs(State.TerrainRoll) ? SettleBlendFast : SettleBlend);

	State.Pitch = FMath::Clamp(State.TerrainPitch + SmoothedPitch, -Params.MaxAngle, Params.MaxAngle);
	State.Roll = FMath::Clamp(State.TerrainRoll + SmoothedBank, -Params.MaxAngle, Params.MaxAngle);

	// Height follows the ground with a lag that stiffens with speed.
	if (Input.bHasGround)
	{
		State.Height = State.bHasHeight ? State.Height + (Input.GroundHeight - State.Height) * HeightBlend : Input.GroundHeight;
		State.bHasHeight = true;
		Output.HeightOffset = State.Height - Input.GroundHeight;
	}

	State.PreviousForward = Input.Forward;
	State.PreviousSpeed = Input.Speed;
	State.TurnFraction = TurnSign * TurnFraction;

	Output.Pitch = State.Pitch;
	Output.Roll = State.Roll;
	Output.SpeedFraction = SpeedFraction;
	Output.TurnFraction = TurnFraction;
	return Output;
}
