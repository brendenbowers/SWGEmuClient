#include "Components/SWGGroupComponent.h"
#include "Network/SWGPacket.h"

USWGGroupComponent::USWGGroupComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGGroupComponent::ApplyBase6(FSWGPacket& Packet)
{
	GroupId = Packet.ReadInt64();
	GroupInviterId = Packet.ReadInt64();
	GroupInviteCounter = Packet.ReadInt64();
	bHasBase6 = true;
}
