#include "Components/SWGPerformanceComponent.h"

USWGPerformanceComponent::USWGPerformanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGPerformanceComponent::ApplyBase6(const FCreatureObjectBaseline& Baseline)
{
	PerformanceAnimation = Baseline.PerformanceAnimation;
	MoodString = Baseline.MoodString;
	MoodId = Baseline.MoodId;
	PerformanceStartTime = Baseline.PerformanceStartTime;
	PerformanceType = Baseline.PerformanceType;
	bHasBase6 = true;
}

void USWGPerformanceComponent::ApplyDelta6(const FCreatureObjectDelta& Delta)
{
	if (Delta.PerformanceAnimation.IsSet()) { PerformanceAnimation = *Delta.PerformanceAnimation; }
	if (Delta.MoodString.IsSet())           { MoodString = *Delta.MoodString; }
	if (Delta.MoodId.IsSet())               { MoodId = *Delta.MoodId; }
	if (Delta.PerformanceStartTime.IsSet()) { PerformanceStartTime = *Delta.PerformanceStartTime; }
	if (Delta.PerformanceType.IsSet())      { PerformanceType = *Delta.PerformanceType; }
}
