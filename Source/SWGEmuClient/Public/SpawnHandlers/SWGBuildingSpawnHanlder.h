

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"


struct FSWGPobCell;
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

	static void FinishCell(ASWGCell* CellActor, ASWGBuilding* BuildingActor, int32 CellIndex, TObjectPtr<USWGTreSubsystem> TreSubsystem, TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem);

private:
	static TWeakObjectPtr<UDataTable> GetDoorStyleTable();
};
