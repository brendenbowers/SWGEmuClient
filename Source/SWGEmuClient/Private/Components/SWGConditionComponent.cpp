#include "Components/SWGConditionComponent.h"
#include "Network/SWGPacket.h"

USWGConditionComponent::USWGConditionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGConditionComponent::ApplyBase3(const FTangibleObjectBaseline& Baseline)
{
	UseCount = Baseline.UseCount;
	ConditionDamage = Baseline.ConditionDamage;
	MaxCondition = Baseline.MaxCondition;
	bHasBase3 = true;
}
