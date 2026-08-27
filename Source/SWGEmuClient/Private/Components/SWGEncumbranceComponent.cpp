#include "Components/SWGEncumbranceComponent.h"

USWGEncumbranceComponent::USWGEncumbranceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGEncumbranceComponent::ApplyBase4(const FCreatureObjectBaseline& Baseline)
{
	Encumbrances = Baseline.Encumbrances;
	bHasBase4 = true;
}

void USWGEncumbranceComponent::ApplyDelta4(const FCreatureObjectDelta& Delta)
{
	ApplyIndexedListChanges(Delta.Encumbrances, Encumbrances);
}
