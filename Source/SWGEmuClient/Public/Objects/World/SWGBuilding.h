#pragma once

#include "CoreMinimal.h"
#include "Objects/SWGObject.h"
#include "TRE/SWGPobReader.h"
#include "TRE/SWGInteriorLayoutReader.h"
#include "Subsystems/SWGTerrainSubsystem.h"
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

	/** Client-side furniture from the template's interiorLayoutFileName (.ilf), spawned per room by FSWGCellSpawnHandler::FinishCell. */
	FSWGInteriorLayoutData InteriorLayout;

	/** Called by FSWGCellSpawnHandler::FinishCell once Cell->TriggerVolume exists. */
	void RegisterCellTrigger(ASWGCell* Cell, bool bCanSeeParent);

	/**
	 * True while the local player overlaps a cell trigger, stands within the
	 * exterior footprint, or is reported by the server as being in one of the
	 * rooms. The extra signals cover trigger-less spaces (a starport's open
	 * courtyard), where the rooms would otherwise be torn down underfoot.
	 */
	bool IsPlayerInside() const { return InteriorOverlapCount > 0 || bPlayerWithinFootprint; }

	/** World-space XY bounds of the exterior shell, for the streaming distance and footprint tests. */
	FBox2D GetFootprint() const;

	/** Set by USWGInteriorStreamingSubsystem each sweep. */
	void SetPlayerWithinFootprint(bool bWithin) { bPlayerWithinFootprint = bWithin; }

	// ── Range-streamed interior (USWGInteriorStreamingSubsystem) ──────────

	/** A network cell that arrived while the player was out of range — its actor exists, unfinished. */
	struct FDeferredNetworkCell
	{
		TWeakObjectPtr<ASWGCell> Cell;
		int32 CellIndex = 0;
	};
	TArray<FDeferredNetworkCell> DeferredNetworkCells;

	/** .ws rooms (with their props). Kept after loading so an unload can be reversed. */
	TArray<FSWGWorldSnapshotSpawnInfo> DeferredSnapshotCells;

	/** The .ws placement, captured before the mesh's yaw correction replaces the root — DeferredSnapshotCells are relative to this. */
	FTransform SnapshotTransform;

	bool HasDeferredInterior() const { return !DeferredNetworkCells.IsEmpty() || !DeferredSnapshotCells.IsEmpty(); }

	/** True if CellObjectId is one of this building's rooms, loaded or not. */
	bool OwnsCell(int64 CellObjectId) const;

	/**
	 * Rooms stream in two tiers, decided by USWGInteriorStreamingSubsystem:
	 * bClosed=false is the rooms whose portal opens to the exterior (POB
	 * canSeeParentCell) — loaded by distance and view cone; bClosed=true is
	 * everything else — loaded only when close. A network cell is finished
	 * once and stays; a .ws room is destroyed again by UnloadRooms.
	 */
	bool AreRoomsLoaded(bool bClosed) const { return bClosed ? bClosedRoomsLoaded : bVisibleRoomsLoaded; }
	void LoadRooms(bool bClosed);
	void UnloadRooms(bool bClosed);

	/** Whether the POB marks cell CellIndex as a closed room (no portal to the exterior). */
	bool IsClosedRoom(int32 CellIndex) const;

private:
	/** .ws cells created by LoadRooms, so UnloadRooms can take exactly those down. */
	TArray<TWeakObjectPtr<ASWGCell>> StreamedCells;

	bool bVisibleRoomsLoaded = false;
	bool bClosedRoomsLoaded = false;

	UFUNCTION()
	void OnCellTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnCellTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void SetExteriorShellHidden(bool bShouldHide);

	/** Counted, not a bool, so straddling two triggers in a doorway doesn't reveal the shell early. */
	int32 NonSeeThroughOverlapCount = 0;

	/** Same idea for every cell trigger — the player is "inside" while any of them overlaps. */
	int32 InteriorOverlapCount = 0;

	bool bPlayerWithinFootprint = false;

	TMap<TWeakObjectPtr<UPrimitiveComponent>, bool> CellSeeParentByTrigger;
};
