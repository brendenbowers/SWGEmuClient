#include "Components/SWGDefenderComponent.h"

USWGDefenderComponent::USWGDefenderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGDefenderComponent::ApplyBase6(FSWGPacket& Packet)
{
	// TODO: DefenderList from TANO base6.
}
