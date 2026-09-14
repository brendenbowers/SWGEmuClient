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

	// A baseline is the first time anything downstream sees this creature's
	// posture, so broadcast unconditionally rather than diffing — Upright with
	// no states matches the zero-initialized defaults but is a real posture,
	// not "unset".
	OnPostureOrStateChanged.Broadcast(GetPosture(), StateBitmask);
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
	const uint8 PreviousPosture = Posture;
	const int64 PreviousStateBitmask = StateBitmask;

	if (Delta.Posture.IsSet())      { Posture = *Delta.Posture; }
	if (Delta.FactionRank.IsSet())  { FactionRank = *Delta.FactionRank; }
	if (Delta.StateBitmask.IsSet()) { StateBitmask = *Delta.StateBitmask; }

	BroadcastIfPostureOrStateChanged(PreviousPosture, PreviousStateBitmask);
}

void USWGCombatStateComponent::ApplyDelta6(const FCreatureObjectDelta& Delta)
{
	if (Delta.WeaponId.IsSet()) { WeaponId = *Delta.WeaponId; }
	if (Delta.TargetId.IsSet()) { TargetId = *Delta.TargetId; }
}

void USWGCombatStateComponent::BroadcastIfPostureOrStateChanged(uint8 PreviousPosture, int64 PreviousStateBitmask)
{
	if (Posture != PreviousPosture || StateBitmask != PreviousStateBitmask)
	{
		OnPostureOrStateChanged.Broadcast(GetPosture(), StateBitmask);
	}
}
