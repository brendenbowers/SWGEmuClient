

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"

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
