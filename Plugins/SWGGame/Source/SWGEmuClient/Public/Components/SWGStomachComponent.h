#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Player/PlayerObjectBaseline.h"
#include "Network/Objects/Zone/Player/PlayerObjectDelta.h"
#include "SWGStomachComponent.generated.h"

/** PLAY base9 — food and drink fill, the caps on buff consumption. */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGStomachComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGStomachComponent();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 FoodFilling = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 FoodFillingMax = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 DrinkFilling = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 DrinkFillingMax = 0;

	bool bHasBase9 = false;

	void ApplyBase9(const FPlayerObjectBaseline& Baseline);
	void ApplyDelta9(const FPlayerObjectDelta& Delta);
};
