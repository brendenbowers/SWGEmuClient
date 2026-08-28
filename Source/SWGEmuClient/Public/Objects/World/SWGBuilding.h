#pragma once

#include "CoreMinimal.h"
#include "Objects/SWGObject.h"
#include "TRE/SWGPobReader.h"
#include "SWGBuilding.generated.h"

class ASWGCell;
class ASWGDoor;
class UPrimitiveComponent;
class USWGTangibleComponent;
class USWGConditionComponent;
class USWGDefenderComponent;

/**
 * Buildings. The BUIO FourCC exists but isn't what arrives: live traffic sends
 * buildings as TANO, which is why these are plain tangible components rather
 * than anything building-specific.
 */
UCLASS()
class SWGEMUCLIENT_API ASWGBuilding : public ASWGObject
{
	GENERATED_BODY()

public:
	ASWGBuilding();

	// Buildings arrive on the wire as TANO, not BUIO, so they carry the same
	// name/condition/defender state as any other tangible.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGTangibleComponent> TangibleComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGConditionComponent> ConditionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGDefenderComponent> DefenderComponent;

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
