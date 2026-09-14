#include "Subsystems/SWGDeltaHandlerRegistry.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "GameFramework/Actor.h"

FSWGDeltaHandlerRegistry& FSWGDeltaHandlerRegistry::Get()
{
	// Function-local static avoids static initialization order issues.
	static FSWGDeltaHandlerRegistry Instance;
	return Instance;
}

void FSWGDeltaHandlerRegistry::Register(TArray<ESWGObjectType> ObjectTypes, FSWGDeltaHandlerFactory&& Factory)
{
	if (!Factory)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGDeltaHandlerRegistry: attempted to register a null handler factory"));
		return;
	}

	if (ObjectTypes.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGDeltaHandlerRegistry: attempted to register a handler for no object types — it would never be reached"));
		return;
	}

	const int32 EntryIndex = Entries.Add(FEntry{MoveTemp(Factory), nullptr});

	for (const ESWGObjectType ObjectType : ObjectTypes)
	{
		HandlerIndicesByType.FindOrAdd(ObjectType).Add(EntryIndex);
	}
}

ISWGDeltaHandler* FSWGDeltaHandlerRegistry::ResolveHandler(const FEntry& Entry)
{
	if (!Entry.Handler)
	{
		Entry.Handler = Entry.Factory();
	}
	return Entry.Handler.Get();
}

bool FSWGDeltaHandlerRegistry::TryHandle(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments& Args) const
{
	const TArray<int32>* Indices = HandlerIndicesByType.Find(Msg.GetObjectType());
	if (!Indices)
	{
		return false;
	}

	for (const int32 Index : *Indices)
	{
		ISWGDeltaHandler* Handler = ResolveHandler(Entries[Index]);
		if (!Handler || !Handler->CanHandleDelta(Actor, Msg))
		{
			continue;
		}

		if (Handler->HandleDelta(Actor, Msg, Args))
		{
			return true;
		}
	}

	return false;
}

bool FSWGDeltaHandlerRegistry::HasHandlerFor(const AActor& Actor, const FDeltasMessage& Msg) const
{
	const TArray<int32>* Indices = HandlerIndicesByType.Find(Msg.GetObjectType());
	if (!Indices)
	{
		return false;
	}

	for (const int32 Index : *Indices)
	{
		const ISWGDeltaHandler* Handler = ResolveHandler(Entries[Index]);
		if (Handler && Handler->CanHandleDelta(Actor, Msg))
		{
			return true;
		}
	}

	return false;
}
