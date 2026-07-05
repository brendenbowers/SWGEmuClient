#include "Components/SWGGroupComponent.h"

USWGGroupComponent::USWGGroupComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGGroupComponent::ApplyBase6(FSWGPacket& Packet)
{
	// TODO: GroupId/GroupInviterId/GroupInviteCounter from CREO base6.
}
