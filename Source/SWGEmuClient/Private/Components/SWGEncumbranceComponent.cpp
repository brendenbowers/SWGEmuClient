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
