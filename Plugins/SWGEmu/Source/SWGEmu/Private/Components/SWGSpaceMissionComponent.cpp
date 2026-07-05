#include "Components/SWGSpaceMissionComponent.h"

USWGSpaceMissionComponent::USWGSpaceMissionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGSpaceMissionComponent::ApplyBase4(FSWGPacket& Packet)
{
	// TODO: ListenId/SpaceMissionObjects from CREO base4.
}
