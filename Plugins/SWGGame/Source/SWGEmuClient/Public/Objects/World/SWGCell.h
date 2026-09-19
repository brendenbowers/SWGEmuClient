#pragma once

#include "CoreMinimal.h"
#include "Objects/SWGObject.h"
#include "SWGCell.generated.h"

class ASWGBuilding;
class UPrimitiveComponent;
class ULightComponent;

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

	// POB canSeeParentCell: the room's portal opens onto the exterior. Decides
	// which streaming tier the room loads in (USWGInteriorStreamingSubsystem).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	bool bCanSeeParent = false;

	// Client-only props placed in this room (.ws children, interior layout),
	// destroyed with it by ASWGBuilding::UnloadRoom. Unattached: the cell's
	// root is replaced when its mesh lands.
	TArray<TWeakObjectPtr<AActor>> InteriorActors;

	/**
	 * The room's own lights from the POB (ambient, parallels, points), on
	 * lighting channel 1 with the room's geometry. Retail lit a room only by
	 * its own set, so ASWGBuilding switches on just the room the player is in.
	 */
	UPROPERTY(VisibleAnywhere, Category = "SWGEmu")
	TArray<TObjectPtr<ULightComponent>> RoomLights;

	/** As built from the POB, before swg.RoomLightScale — parallel to RoomLights. */
	TArray<float> RoomLightBaseIntensity;

	void AddRoomLight(ULightComponent* Light);
	void SetRoomLightsEnabled(bool bEnabled);
};
