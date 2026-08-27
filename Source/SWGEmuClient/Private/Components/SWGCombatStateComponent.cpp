#include "Components/SWGCombatStateComponent.h"

USWGCombatStateComponent::USWGCombatStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGCombatStateComponent::ApplyBase3(const FCreatureObjectBaseline& Baseline)
{
	Posture = Baseline.Posture;
	FactionRank = Baseline.FactionRank;
	StateBitmask = Baseline.StateBitmask;
	bHasBase3 = true;
}

void USWGCombatStateComponent::ApplyBase6(const FCreatureObjectBaseline& Baseline)
{
	WeaponId = Baseline.WeaponId;
	TargetId = Baseline.TargetId;
	Frozen = Baseline.Frozen;
	bHasBase6 = true;
}

void USWGCombatStateComponent::ApplyDelta3(const FCreatureObjectDelta& Delta)
{
	if (Delta.Posture.IsSet())      { Posture = *Delta.Posture; }
	if (Delta.FactionRank.IsSet())  { FactionRank = *Delta.FactionRank; }
	if (Delta.StateBitmask.IsSet()) { StateBitmask = *Delta.StateBitmask; }
}

void USWGCombatStateComponent::ApplyDelta6(const FCreatureObjectDelta& Delta)
{
	if (Delta.WeaponId.IsSet()) { WeaponId = *Delta.WeaponId; }
	if (Delta.TargetId.IsSet()) { TargetId = *Delta.TargetId; }
}
