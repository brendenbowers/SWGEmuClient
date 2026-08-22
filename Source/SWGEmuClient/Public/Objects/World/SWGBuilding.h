#pragma once

#include "CoreMinimal.h"
#include "Objects/SWGObject.h"
#include "TRE/SWGPobReader.h"
#include "SWGBuilding.generated.h"

class ASWGCell;
class ASWGDoor;
class UPrimitiveComponent;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TArray<TObjectPtr<ASWGDoor>> Doors;


	FSWGPobData PortalData;

	/** Called by FSWGCellSpawnHandler::FinishCell once Cell->TriggerVolume exists. */
	void RegisterCellTrigger(ASWGCell* Cell, bool bCanSeeParent);

private:
	UFUNCTION()
	void OnCellTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnCellTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void SetExteriorShellHidden(bool bShouldHide);

	/** Counted, not a bool, so straddling two triggers in a doorway doesn't reveal the shell early. */
	int32 NonSeeThroughOverlapCount = 0;

	TMap<TWeakObjectPtr<UPrimitiveComponent>, bool> CellSeeParentByTrigger;
};
