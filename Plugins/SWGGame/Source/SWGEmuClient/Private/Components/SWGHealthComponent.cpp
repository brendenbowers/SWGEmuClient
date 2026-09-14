#include "Components/SWGHealthComponent.h"

USWGHealthComponent::USWGHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGHealthComponent::ApplyBase1(const FCreatureObjectBaseline& Baseline)
{
	BaseHAM = Baseline.BaseHAM;
	bHasBase1 = true;
}

void USWGHealthComponent::ApplyBase3(const FCreatureObjectBaseline& Baseline)
{
	ShockWounds = Baseline.ShockWounds;
	Wounds = Baseline.Wounds;
	bHasBase3 = true;
}

void USWGHealthComponent::ApplyBase6(const FCreatureObjectBaseline& Baseline)
{
	HAM = Baseline.HAM;
	MaxHAM = Baseline.MaxHAM;
	bHasBase6 = true;
}

void USWGHealthComponent::ApplyDelta1(const FCreatureObjectDelta& Delta)
{
	ApplyIndexedListChanges(Delta.BaseHAM, BaseHAM);
}

void USWGHealthComponent::ApplyDelta3(const FCreatureObjectDelta& Delta)
{
	if (Delta.ShockWounds.IsSet()) { ShockWounds = *Delta.ShockWounds; }
	ApplyIndexedListChanges(Delta.Wounds, Wounds);
}

void USWGHealthComponent::ApplyDelta6(const FCreatureObjectDelta& Delta)
{
	ApplyIndexedListChanges(Delta.HAM, HAM);
	ApplyIndexedListChanges(Delta.MaxHAM, MaxHAM);
}
