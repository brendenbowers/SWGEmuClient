#pragma once

#include "CoreMinimal.h"
#include "Objects/SWGObject.h"
#include "SWGBuilding.generated.h"

class ASWGCell;

/**
 * BUIO. Placeholder only — the live baseline construction path wasn't found
 * in the Core3 survey (BuildingObjectMessage3/6.h are stale/commented-out
 * reference files, FourCC confirmed as 0x4255494F). Needs a follow-up grep
 * in BuildingObjectImplementation.cpp before this gets real baseline fields.
 * See world-object-plan.html "Buildings (BUIO) & Cells (TLCS)".
 */
UCLASS()
class SWGEMUCLIENT_API ASWGBuilding : public ASWGObject
{
	GENERATED_BODY()

public:
	ASWGBuilding() = default;

	/** Populated via CellObject's "parent building" reference once cells spawn (createCellObjects()). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TArray<TObjectPtr<ASWGCell>> Cells;
};
