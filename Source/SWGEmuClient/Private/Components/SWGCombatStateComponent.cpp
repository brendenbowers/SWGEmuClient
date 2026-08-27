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
