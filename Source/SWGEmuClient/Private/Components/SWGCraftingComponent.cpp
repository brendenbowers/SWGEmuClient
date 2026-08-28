#include "Components/SWGCraftingComponent.h"

USWGCraftingComponent::USWGCraftingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGCraftingComponent::ApplyBase9(const FPlayerObjectBaseline& Baseline)
{
	Schematics = Baseline.Schematics;
	bHasBase9 = true;
}

void USWGCraftingComponent::ApplyDelta9(const FPlayerObjectDelta& Delta)
{
	if (Delta.CraftingState.IsSet())          { CraftingState = *Delta.CraftingState; }
	if (Delta.ClosestCraftingStation.IsSet()) { ClosestCraftingStation = *Delta.ClosestCraftingStation; }
	if (Delta.ExperimentationFlag.IsSet())    { ExperimentationFlag = *Delta.ExperimentationFlag; }
	if (Delta.ExperimentationPoints.IsSet())  { ExperimentationPoints = *Delta.ExperimentationPoints; }

	ApplyIndexedListChanges(Delta.Schematics, Schematics);
}
