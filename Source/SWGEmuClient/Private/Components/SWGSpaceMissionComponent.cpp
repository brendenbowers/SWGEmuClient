#include "Components/SWGSpaceMissionComponent.h"

USWGSpaceMissionComponent::USWGSpaceMissionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGSpaceMissionComponent::ApplyBase4(const FCreatureObjectBaseline& Baseline)
{
	ListenId = Baseline.ListenId;
	SpaceMissionObjects = Baseline.SpaceMissionObjects;
	bHasBase4 = true;
}
