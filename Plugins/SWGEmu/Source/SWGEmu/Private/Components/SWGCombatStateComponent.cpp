#include "Components/SWGCombatStateComponent.h"

USWGCombatStateComponent::USWGCombatStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGCombatStateComponent::ApplyBase3(FSWGPacket& Packet)
{
	// TODO: Posture/FactionRank/StateBitmask from CREO base3.
}

void USWGCombatStateComponent::ApplyBase6(FSWGPacket& Packet)
{
	// TODO: TargetId/WeaponId/Frozen from CREO base6.
}
