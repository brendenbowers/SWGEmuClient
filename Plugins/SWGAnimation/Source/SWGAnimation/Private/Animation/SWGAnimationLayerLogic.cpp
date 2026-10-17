#include "Animation/SWGAnimationLayerLogic.h"

float SWGAnimLayer::DirectionAngleDegrees(uint8 DirectionCode)
{
	switch (DirectionCode)
	{
		case Direction::Forward:       return 0.0f;
		case Direction::ForwardRight:  return 45.0f;
		case Direction::Right:         return 90.0f;
		case Direction::BackwardRight: return 135.0f;
		case Direction::Backward:      return 180.0f;
		case Direction::BackwardLeft:  return -135.0f;
		case Direction::Left:          return -90.0f;
		case Direction::ForwardLeft:   return -45.0f;
		default:                       return -1000.0f;
	}
}

uint8 SWGAnimLayer::SelectDirection(float ForwardSpeed, float RightSpeed, const TArray<uint8>& Available, uint8 PreviousDirection, float StillSpeed)
{
	if (Available.Num() == 0)
	{
		return Direction::Forward;
	}

	const float Speed = FMath::Sqrt(ForwardSpeed * ForwardSpeed + RightSpeed * RightSpeed);
	if (Speed < StillSpeed && Available.Contains(PreviousDirection))
	{
		return PreviousDirection;
	}

	const float HeadingDegrees = Speed < StillSpeed ? 0.0f : FMath::RadiansToDegrees(FMath::Atan2(RightSpeed, ForwardSpeed));

	uint8 Best = Available[0];
	float BestDistance = TNumericLimits<float>::Max();
	for (const uint8 Code : Available)
	{
		const float Angle = DirectionAngleDegrees(Code);
		if (Angle < -999.0f)
		{
			continue;
		}
		const float Distance = FMath::Abs(FMath::FindDeltaAngleDegrees(HeadingDegrees, Angle));
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Code;
		}
	}
	return Best;
}

SWGAnimLayer::EYawVariant SWGAnimLayer::SelectYawVariant(float YawRateDegreesPerSecond, float ThresholdDegreesPerSecond)
{
	if (YawRateDegreesPerSecond <= -ThresholdDegreesPerSecond)
	{
		return EYawVariant::Negative;
	}
	if (YawRateDegreesPerSecond >= ThresholdDegreesPerSecond)
	{
		return EYawVariant::Positive;
	}
	return EYawVariant::None;
}

float SWGAnimLayer::EffectiveTimeScale(float Scale)
{
	return Scale > KINDA_SMALL_NUMBER ? Scale : 1.0f;
}

float SWGAnimLayer::StepLookAtYaw(float DeltaSeconds, float CurrentYawRadians, float TargetForward, float TargetRight, const FLookAtLimits& Limits)
{
	const float YawClamp = FMath::DegreesToRadians(Limits.YawClampDegrees);
	const float MaxStep = FMath::DegreesToRadians(Limits.MaxHeadTurnDegreesPerSecond) * FMath::Max(DeltaSeconds, 0.0f);

	// A target right on top of the head has no bearing; relax toward forward.
	float DesiredYaw = 0.0f;
	if (TargetForward * TargetForward + TargetRight * TargetRight > KINDA_SMALL_NUMBER)
	{
		DesiredYaw = FMath::Atan2(TargetRight, TargetForward);
	}

	// The head turns the short way round from where it is, so a target passing
	// behind the creature does not whip the head through 360 degrees.
	const float ToDesired = FMath::UnwindRadians(DesiredYaw - CurrentYawRadians);
	DesiredYaw = CurrentYawRadians + ToDesired;
	DesiredYaw = FMath::Clamp(DesiredYaw, -YawClamp, YawClamp);

	const float Step = FMath::Clamp(DesiredYaw - CurrentYawRadians, -MaxStep, MaxStep);
	return CurrentYawRadians + Step;
}

float SWGAnimLayer::ComputeTargetPitch(float HorizontalDistance, float TargetHeightAboveJoint, const FLookAtLimits& Limits)
{
	const float Pitch = FMath::Atan2(TargetHeightAboveJoint, FMath::Max(HorizontalDistance, 0.0f));
	const float Clamp = FMath::DegreesToRadians(Limits.PitchClampDegrees);
	return FMath::Clamp(Pitch, -Clamp, Clamp);
}

bool SWGAnimLayer::UseMovingAction(float HorizontalSpeed, float StillSpeed)
{
	return HorizontalSpeed > StillSpeed;
}

int32 SWGAnimLayer::PickWeighted(const TArray<float>& Weights, float Roll01)
{
	float Total = 0.0f;
	for (const float Weight : Weights)
	{
		Total += FMath::Max(Weight, 0.0f);
	}
	if (Total <= KINDA_SMALL_NUMBER)
	{
		return INDEX_NONE;
	}

	float Remaining = FMath::Clamp(Roll01, 0.0f, 0.999999f) * Total;
	for (int32 Index = 0; Index < Weights.Num(); ++Index)
	{
		Remaining -= FMath::Max(Weights[Index], 0.0f);
		if (Remaining < 0.0f)
		{
			return Index;
		}
	}
	return Weights.Num() - 1;
}

float SWGAnimLayer::OverlayWeight(float Elapsed, float Length, float BlendIn, float BlendOut)
{
	if (Elapsed < 0.0f || Elapsed >= Length)
	{
		return 0.0f;
	}
	float Weight = 1.0f;
	if (BlendIn > KINDA_SMALL_NUMBER)
	{
		Weight = FMath::Min(Weight, Elapsed / BlendIn);
	}
	if (BlendOut > KINDA_SMALL_NUMBER)
	{
		Weight = FMath::Min(Weight, (Length - Elapsed) / BlendOut);
	}
	return FMath::Clamp(Weight, 0.0f, 1.0f);
}
