#include "Components/SWGHealthComponent.h"
#include "Network/SWGPacket.h"

USWGHealthComponent::USWGHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGHealthComponent::ApplyBase1(FSWGPacket& Packet)
{
	BaseHAM = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });
	bHasBase1 = true;
}

void USWGHealthComponent::ApplyBase3Part1(FSWGPacket& Packet)
{
	ShockWounds = Packet.ReadInt32();
}

void USWGHealthComponent::ApplyBase3Part2(FSWGPacket& Packet)
{
	Wounds = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });
	bHasBase3 = true;
}

void USWGHealthComponent::ApplyBase6(FSWGPacket& Packet)
{
	HAM = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });
	MaxHAM = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });
	bHasBase6 = true;
}
