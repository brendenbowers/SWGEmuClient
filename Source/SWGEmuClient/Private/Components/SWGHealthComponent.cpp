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
