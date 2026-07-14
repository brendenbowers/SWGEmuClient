#include "Components/SWGConditionComponent.h"
#include "Network/SWGPacket.h"

USWGConditionComponent::USWGConditionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGConditionComponent::ApplyBase3(FSWGPacket& Packet)
{
	UseCount = Packet.ReadInt32();
	ConditionDamage = Packet.ReadInt32();
	MaxCondition = Packet.ReadInt32();
	bHasBase3 = true;
}
