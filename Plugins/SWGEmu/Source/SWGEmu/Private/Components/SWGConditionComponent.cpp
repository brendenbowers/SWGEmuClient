#include "Components/SWGConditionComponent.h"

USWGConditionComponent::USWGConditionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGConditionComponent::ApplyBase3(FSWGPacket& Packet)
{
	// TODO: ConditionDamage/MaxCondition/UseCount from TANO base3.
}
