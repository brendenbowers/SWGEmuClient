

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"


struct FSWGPobCell;
struct FSWGPobPortalRef;
class ASWGCell;
class ASWGBuilding;
class USWGObjectGraphSubsystem;
class UDataTable;

/**
 *
 */
class SWGEMUCLIENT_API FSWGBuildingSpawnHandler final : public ISWGActorSpawnHandler
{
public:
	FSWGBuildingSpawnHandler() = default;
	~FSWGBuildingSpawnHandler() override = default;
	bool HandleActorSpawn(AActor& Actor, const FSWGActorSpawnArguments& SpawnInfo) override final;

private:
	TObjectPtr<USWGTreSubsystem> TreSubsystem = nullptr;
	TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem = nullptr;
	bool bIsInitialized = false;

	void Initialize(UWorld* World);

};

class SWGEMUCLIENT_API FSWGCellSpawnHandler final : public ISWGActorSpawnHandler
{
public:
	bool HandleActorSpawn(AActor& Actor, const FSWGActorSpawnArguments& SpawnInfo) override final;

	static void CheckAndFinishCell(USWGObjectGraphSubsystem& ObjectGraph, int64 ObjectId, TObjectPtr<USWGTreSubsystem> TreSubsystem, TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem);

	/**
	 * bForceInterior builds the room regardless of the player's distance/view —
	 * ASWGBuilding::LoadRoom's path; otherwise the room is deferred onto the
	 * building until USWGInteriorStreamingSubsystem decides it should load.
	 */
	static void FinishCell(ASWGCell* CellActor, ASWGBuilding* BuildingActor, int32 CellIndex, TObjectPtr<USWGTreSubsystem> TreSubsystem, TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem, bool bForceInterior = false);

	/** Spawns the building's interior layout (.ilf) nodes that belong to this room. */
	static void SpawnInteriorLayout(ASWGCell* CellActor, ASWGBuilding* BuildingActor, const FSWGPobCell& CellData, TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem);

	/**
	 * Spawns a door for each portal that has a door style and a
	 * hardpoint, skipping portals the building already has a door for. Both
	 * cells a portal joins reference it, so whichever side runs first places it.
	 */
	static void SpawnCellDoors(ASWGBuilding* BuildingActor, const FString& CellName, TArrayView<const FSWGPobPortalRef> Portals, TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem);

private:
	static TWeakObjectPtr<UDataTable> GetDoorStyleTable();
};
