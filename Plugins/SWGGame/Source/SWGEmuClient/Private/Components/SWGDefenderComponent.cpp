#include "Components/SWGDefenderComponent.h"
#include "Network/SWGPacket.h"

USWGDefenderComponent::USWGDefenderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGDefenderComponent::ApplyBase6(const FTangibleObjectBaseline& Baseline)
{
	DefenderList = Baseline.DefenderList;
	bHasBase6 = true;
}

void USWGDefenderComponent::ApplyDelta6(const FTangibleObjectDelta& Delta)
{
	ApplyIndexedListChanges(Delta.DefenderList, DefenderList);
}
