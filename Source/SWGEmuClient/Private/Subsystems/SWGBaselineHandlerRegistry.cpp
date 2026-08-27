#include "Subsystems/SWGBaselineHandlerRegistry.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "GameFramework/Actor.h"

FSWGBaselineHandlerRegistry& FSWGBaselineHandlerRegistry::Get()
{
	// Function-local static avoids static initialization order issues.
	static FSWGBaselineHandlerRegistry Instance;
	return Instance;
}

void FSWGBaselineHandlerRegistry::Register(TArray<ESWGObjectType> ObjectTypes, FSWGBaselineHandlerFactory&& Factory)
{
	if (!Factory)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGBaselineHandlerRegistry: attempted to register a null handler factory"));
		return;
	}

	if (ObjectTypes.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGBaselineHandlerRegistry: attempted to register a handler for no object types — it would never be reached"));
		return;
	}

	const int32 EntryIndex = Entries.Add(FEntry{MoveTemp(Factory), nullptr});

	for (const ESWGObjectType ObjectType : ObjectTypes)
	{
		HandlerIndicesByType.FindOrAdd(ObjectType).Add(EntryIndex);
	}
}

ISWGBaselineHandler* FSWGBaselineHandlerRegistry::ResolveHandler(const FEntry& Entry)
{
	if (!Entry.Handler)
	{
		Entry.Handler = Entry.Factory();
	}
	return Entry.Handler.Get();
}

bool FSWGBaselineHandlerRegistry::TryHandle(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args) const
{
	const TArray<int32>* Indices = HandlerIndicesByType.Find(Msg.GetObjectType());
	if (!Indices)
	{
		return false;
	}

	for (const int32 Index : *Indices)
	{
		ISWGBaselineHandler* Handler = ResolveHandler(Entries[Index]);
		if (!Handler || !Handler->CanHandleBaseline(Actor, Msg))
		{
			continue;
		}

		if (Handler->HandleBaseline(Actor, Msg, Args))
		{
			return true;
		}
	}

	return false;
}

bool FSWGBaselineHandlerRegistry::HasHandlerFor(const AActor& Actor, const FBaselinesMessage& Msg) const
{
	const TArray<int32>* Indices = HandlerIndicesByType.Find(Msg.GetObjectType());
	if (!Indices)
	{
		return false;
	}

	for (const int32 Index : *Indices)
	{
		const ISWGBaselineHandler* Handler = ResolveHandler(Entries[Index]);
		if (Handler && Handler->CanHandleBaseline(Actor, Msg))
		{
			return true;
		}
	}

	return false;
}
