#pragma once

#include "CoreMinimal.h"
#include "Objects/SWGObject.h"
#include "SWGCell.generated.h"

class ASWGBuilding;
class UPrimitiveComponent;

UCLASS()
class SWGEMUCLIENT_API ASWGCell : public ASWGObject
{
	GENERATED_BODY()

public:
	ASWGCell() = default;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 CellNumber = 0;

	// Room name from the TLCS baseline — the custom name when the server sent one,
	// otherwise the STF key (e.g. "cantina").
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FString CellName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	FString MeshPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TWeakObjectPtr<ASWGBuilding> OwningBuilding;

	// Null until FSWGCellSpawnHandler::FinishCell builds it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<UPrimitiveComponent> TriggerVolume;
};
