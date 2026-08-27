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
