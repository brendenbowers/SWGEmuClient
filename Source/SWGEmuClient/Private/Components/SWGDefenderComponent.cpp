#include "Components/SWGDefenderComponent.h"
#include "Network/SWGPacket.h"

USWGDefenderComponent::USWGDefenderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGDefenderComponent::ApplyBase6(FSWGPacket& Packet)
{
	// FTangibleObjectBaseline::Unknown076 — "purpose unconfirmed server-side",
	// not mirrored on any component. Consume the bytes to stay aligned, discard.
	Packet.ReadInt32();

	DefenderList = ReadBaselineVector<uint64>(Packet, [](FSWGPacket& P) { return P.ReadUInt64(); });
	bHasBase6 = true;
}
