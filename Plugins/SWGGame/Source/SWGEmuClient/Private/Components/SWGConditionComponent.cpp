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

void USWGConditionComponent::ApplyDelta3(const FTangibleObjectDelta& Delta)
{
	if (Delta.UseCount.IsSet())        { UseCount = *Delta.UseCount; }
	if (Delta.ConditionDamage.IsSet()) { ConditionDamage = *Delta.ConditionDamage; }
	if (Delta.MaxCondition.IsSet())    { MaxCondition = *Delta.MaxCondition; }
}
