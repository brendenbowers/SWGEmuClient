#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "GameFramework/Actor.h"

FSWGActorSpawnHandlerRegistry& FSWGActorSpawnHandlerRegistry::Get()
{
	// Function-local static avoids static initialization order issues.
	static FSWGActorSpawnHandlerRegistry Instance;
	return Instance;
}

void FSWGActorSpawnHandlerRegistry::Register(UClass* ActorClass, FSWGActorSpawnHandlerFactory&& Factory)
{
	if (!ActorClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGActorSpawnHandlerRegistry: attempted to register a handler for a null ActorClass"));
		return;
	}

	if (Factories.Contains(ActorClass))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGActorSpawnHandlerRegistry: %s already has a registered spawn handler — overwriting"), *ActorClass->GetName());
	}
	Factories.Add(ActorClass, MoveTemp(Factory));
}

bool FSWGActorSpawnHandlerRegistry::TryHandle(AActor& Actor, const FSWGActorSpawnArguments& SpawnInfo) const
{
	const FSWGActorSpawnHandlerFactory* Factory = Factories.Find(Actor.GetClass());
	if (!Factory)
	{
		return false;
	}

	const TSharedPtr<ISWGActorSpawnHandler> Handler = (*Factory)();
	if (!Handler)
	{
		return false;
	}

	return Handler->HandleActorSpawn(Actor, SpawnInfo);
}

bool FSWGActorSpawnHandlerRegistry::IsRegistered(UClass* ActorClass) const
{
	return ActorClass && Factories.Contains(ActorClass);
}
