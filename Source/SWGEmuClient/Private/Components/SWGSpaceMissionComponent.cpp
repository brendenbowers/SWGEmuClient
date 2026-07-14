#include "Components/SWGSpaceMissionComponent.h"
#include "Network/SWGPacket.h"

USWGSpaceMissionComponent::USWGSpaceMissionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGSpaceMissionComponent::ApplyBase4Part1(FSWGPacket& Packet)
{
	ListenId = Packet.ReadInt64();
}

void USWGSpaceMissionComponent::ApplyBase4Part2(FSWGPacket& Packet)
{
	SpaceMissionObjects = ReadBaselineVector<FGroupMissionCriticalObject>(Packet, [](FSWGPacket& P)
	{
		FGroupMissionCriticalObject Obj;
		Obj.Deserialize(P);
		return Obj;
	});
	bHasBase4 = true;
}
