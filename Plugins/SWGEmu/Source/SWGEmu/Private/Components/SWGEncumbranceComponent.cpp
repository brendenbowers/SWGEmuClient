#include "Components/SWGEncumbranceComponent.h"
#include "Network/SWGPacket.h"

USWGEncumbranceComponent::USWGEncumbranceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGEncumbranceComponent::ApplyBase4(FSWGPacket& Packet)
{
	Encumbrances = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });
	bHasBase4 = true;
}
