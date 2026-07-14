#include "Components/SWGCombatStateComponent.h"
#include "Network/SWGPacket.h"

USWGCombatStateComponent::USWGCombatStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGCombatStateComponent::ApplyBase3Part1(FSWGPacket& Packet)
{
	Posture = Packet.ReadByte();
	FactionRank = Packet.ReadByte();
}

void USWGCombatStateComponent::ApplyBase3Part2(FSWGPacket& Packet)
{
	StateBitmask = Packet.ReadInt64();
	bHasBase3 = true;
}

void USWGCombatStateComponent::ApplyBase6Part1(FSWGPacket& Packet)
{
	WeaponId = Packet.ReadInt64();
}

void USWGCombatStateComponent::ApplyBase6Part2(FSWGPacket& Packet)
{
	TargetId = Packet.ReadInt64();
}

void USWGCombatStateComponent::ApplyBase6Part3(FSWGPacket& Packet)
{
	Frozen = Packet.ReadByte();
	bHasBase6 = true;
}
