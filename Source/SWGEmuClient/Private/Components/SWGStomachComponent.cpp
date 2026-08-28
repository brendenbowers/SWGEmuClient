#include "Components/SWGStomachComponent.h"

USWGStomachComponent::USWGStomachComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGStomachComponent::ApplyBase9(const FPlayerObjectBaseline& Baseline)
{
	FoodFilling = Baseline.FoodFilling;
	FoodFillingMax = Baseline.FoodFillingMax;
	DrinkFilling = Baseline.DrinkFilling;
	DrinkFillingMax = Baseline.DrinkFillingMax;
	bHasBase9 = true;
}

void USWGStomachComponent::ApplyDelta9(const FPlayerObjectDelta& Delta)
{
	if (Delta.FoodFilling.IsSet())     { FoodFilling = *Delta.FoodFilling; }
	if (Delta.FoodFillingMax.IsSet())  { FoodFillingMax = *Delta.FoodFillingMax; }
	if (Delta.DrinkFilling.IsSet())    { DrinkFilling = *Delta.DrinkFilling; }
	if (Delta.DrinkFillingMax.IsSet()) { DrinkFillingMax = *Delta.DrinkFillingMax; }
}
