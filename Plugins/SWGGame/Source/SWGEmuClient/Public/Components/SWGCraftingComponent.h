#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Player/PlayerObjectBaseline.h"
#include "Network/Objects/Zone/Player/PlayerObjectDelta.h"
#include "SWGCraftingComponent.generated.h"

/**
 * PLAY base9 — known schematics and crafting-session state. Everything but the
 * schematic list arrives only as a delta; the baseline sends zeroes for it.
 */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGCraftingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGCraftingComponent();

	TSWGBaselineList<FDraftSchematic> Schematics;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 CraftingState = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int64 ClosestCraftingStation = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 ExperimentationFlag = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 ExperimentationPoints = 0;

	bool bHasBase9 = false;

	void ApplyBase9(const FPlayerObjectBaseline& Baseline);
	void ApplyDelta9(const FPlayerObjectDelta& Delta);
};
