#pragma once

#include "CoreMinimal.h"

class AActor;
struct FSWGWorldSnapshotSpawnInfo;
class USWGMeshGeneratorSubsystem;


struct FSWGActorSpawnArguments
{
	uint32 TemplateCrc;
	TSubclassOf<AActor> ActorClass;
	FString TemplateName;
};

/**
 * Implemented by a class that wants to take over spawn-time generation for
 * one particular actor class, in place of the default generic path
 * 
 * Actor is already spawned (SpawnActor already ran) by the time HandleActorSpawn
 * runs — this continues whatever class-specific generation is needed
 * (spawning further child actors, calling into MeshGenerator for pieces that
 * don't need special handling, etc).
 */
class ISWGActorSpawnHandler
{
public:
	virtual ~ISWGActorSpawnHandler() = default;

	virtual bool HandleActorSpawn(AActor& Actor, const FSWGActorSpawnArguments& SpawnInfo) = 0;
};

using FSWGActorSpawnHandlerFactory = TFunction<TSharedPtr<ISWGActorSpawnHandler>()>;

/**
 * Maps an actor class to the handler responsible for continuing its
 * spawn-time generation. Populated at static-init time by
 * TSWGActorSpawnHandlerRegistrar<T> — see REGISTER_SWG_ACTOR_SPAWN_HANDLER.
 * USWGTerrainSubsystem::SpawnWorldSnapshotObjects calls TryHandle() for each
 * spawned world-snapshot actor and only falls back to
 * USWGMeshGeneratorSubsystem::RequestMeshForTemplatePath when nothing is
 * registered for that actor's class.
 */
class SWGEMUCLIENT_API FSWGActorSpawnHandlerRegistry
{
public:
	static FSWGActorSpawnHandlerRegistry& Get();

	void Register(UClass* ActorClass, FSWGActorSpawnHandlerFactory&& Factory);

	/**
	 * Constructs and runs the handler registered for Actor's class, if any.
	 * Returns false (and does nothing) when no handler is registered — the
	 * caller should fall back to the generic mesh pipeline in that case.
	 */
	bool TryHandle(AActor& Actor, const FSWGActorSpawnArguments& SpawnInfo) const;

	bool IsRegistered(UClass* ActorClass) const;

private:
	TMap<UClass*, FSWGActorSpawnHandlerFactory> Factories;
};

/**
 * TSWGActorSpawnHandlerRegistrar<T> — place a static instance of this in a
 * handler's .cpp to self-register at startup. Use the
 * REGISTER_SWG_ACTOR_SPAWN_HANDLER macro rather than this directly.
 */
template<typename THandler>
struct TSWGActorSpawnHandlerRegistrar
{
	explicit TSWGActorSpawnHandlerRegistrar(UClass* ActorClass)
	{
		FSWGActorSpawnHandlerRegistry::Get().Register(ActorClass, []() -> TSharedPtr<ISWGActorSpawnHandler>
		{
			return MakeShared<THandler>();
		});
	}
};

/**
 * Place this in a handler's .cpp file to self-register it as the spawn
 * handler for ActorClass.
 *
 * Example:
 *   REGISTER_SWG_ACTOR_SPAWN_HANDLER(FSWGBuildingSpawnHandler, ASWGBuilding)
 */
#define REGISTER_SWG_ACTOR_SPAWN_HANDLER(HandlerType, ActorClass) \
	static TSWGActorSpawnHandlerRegistrar<HandlerType> GRegistrar_##HandlerType(ActorClass::StaticClass());
