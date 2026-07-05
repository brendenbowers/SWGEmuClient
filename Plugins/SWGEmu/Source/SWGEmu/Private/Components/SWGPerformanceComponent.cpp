#include "Components/SWGPerformanceComponent.h"

USWGPerformanceComponent::USWGPerformanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGPerformanceComponent::ApplyBase6(FSWGPacket& Packet)
{
	// TODO: PerformanceAnimation/MoodString/MoodId/PerformanceStartTime/PerformanceType from CREO base6.
}
