#pragma once

#include "CoreMinimal.h"
#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "Components/SWGTerminalComponent.h"

class USWGTreSubsystem;

/**
 * Registered for ASWGItem: classifies object/tangible/terminal/*.iff
 * templates and attaches USWGTerminalComponent when the template is one.
 * Always returns false — tagging only, never a substitute for the generic
 * mesh-gen fallback every other ASWGItem still needs (see
 * USWGObjectGraphSubsystem::HandleSceneCreateObject and
 * USWGTerrainSubsystem::SpawnWorldSnapshotNode, both of which run that
 * fallback only when TryHandle comes back false).
 */
class SWGEMUCLIENT_API FSWGTerminalSpawnHandler final : public ISWGActorSpawnHandler
{
public:
	bool HandleActorSpawn(AActor& Actor, const FSWGActorSpawnArguments& SpawnInfo) override final;

	/** Exposed for testing: classifies a raw gameObjectType value (SceneObjectType.h). Returns false if it's outside the terminal family (0x4000-0x40xx) entirely. */
	static bool ClassifyGameObjectType(int32 GameObjectType, ESWGTerminalType& OutType);
};
