#pragma once

#include "CoreMinimal.h"
#include "Objects/SWGObject.h"
#include "SWGCell.generated.h"

class ASWGBuilding;

/**
 * TLCS — a building's interior. Confirmed a separate baseline object, keyed
 * by cellNumber (not object id — CellObjectMessage3.h:21), spawned by its
 * owning building via createCellObjects(). Needs a parent-building reference
 * rather than going through the ObjectId registry alone. See
 * world-object-plan.html "Buildings (BUIO) & Cells (TLCS)".
 */
UCLASS()
class SWGEMUCLIENT_API ASWGCell : public ASWGObject
{
	GENERATED_BODY()

public:
	ASWGCell() = default;

	int32 CellNumber = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<ASWGBuilding> OwningBuilding;
};
