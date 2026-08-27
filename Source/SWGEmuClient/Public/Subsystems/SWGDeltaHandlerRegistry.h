#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGFourCC.h"

class AActor;
struct FDeltasMessage;
class USWGObjectGraphSubsystem;

/**
 * Additional Context the Delta Hanlder can use to handle deltas
 */
struct FSWGDeltaArguments
{
	USWGObjectGraphSubsystem* ObjectGraph = nullptr;
};

/**
 * Contract to define a class that will handle deltas, should be registered with the
 * REGISTER_SWG_DELTA_HANDLER macro
 */
class ISWGDeltaHandler
{
public:
	virtual ~ISWGDeltaHandler() = default;

	/**
	 * Once a handler is resolved by its ESWGObjectType, this check is called to see if the handler can handle the message
	 */
	virtual bool CanHandleDelta(const AActor& Actor, const FDeltasMessage& Msg) const = 0;

	/**
	 * Handles the delta message by processing and applying it to the given actor. Return false to allow another handler to process the request
	 * or if it cant be handled. NB: The CanHandleDelta should be accurate on if this can handle the request.
	 */
	virtual bool HandleDelta(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments& Args) = 0;
};

using FSWGDeltaHandlerFactory = TFunction<TSharedPtr<ISWGDeltaHandler>()>;

/**
 * Registery to resolve delta handlers.
 */
class SWGEMUCLIENT_API FSWGDeltaHandlerRegistry
{
public:
	static FSWGDeltaHandlerRegistry& Get();

	/**
	 * Registers one handler for every objet type in the list
	 */
	void Register(TArray<ESWGObjectType> ObjectTypes, FSWGDeltaHandlerFactory&& Factory);

	/**
	 * Resolves the handlers registered for the message type and runs until a handler that claims the message completes successfuly
	 */
	bool TryHandle(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments& Args) const;

	/** Determines if a hanlder can handle the the message */
	bool HasHandlerFor(const AActor& Actor, const FDeltasMessage& Msg) const;

	/** Returnes true if any handlers exist for the object type */
	bool IsRegistered(ESWGObjectType ObjectType) const { return HandlerIndicesByType.Contains(ObjectType); }

private:
	struct FEntry
	{
		FSWGDeltaHandlerFactory Factory;
		mutable TSharedPtr<ISWGDeltaHandler> Handler;
	};

	static ISWGDeltaHandler* ResolveHandler(const FEntry& Entry);

	TArray<FEntry> Entries;

	TMap<ESWGObjectType, TArray<int32>> HandlerIndicesByType;
};

template<typename THandler>
struct TSWGDeltaHandlerRegistrar
{
	explicit TSWGDeltaHandlerRegistrar(std::initializer_list<ESWGObjectType> ObjectTypes)
	{
		FSWGDeltaHandlerRegistry::Get().Register(TArray<ESWGObjectType>(ObjectTypes), []() -> TSharedPtr<ISWGDeltaHandler>
		{
			return MakeShared<THandler>();
		});
	}
};

/**
 * Use this in a handler's .cpp file to register as the delta
 * handler for the given object types.
 *
 * Example:
 *   REGISTER_SWG_DELTA_HANDLER(FSWGTangibleDeltaHandler,
 *       ESWGObjectType::TANO, ESWGObjectType::WEAO)
 */
#define REGISTER_SWG_DELTA_HANDLER(HandlerType, ...) \
	static TSWGDeltaHandlerRegistrar<HandlerType> GDeltaRegistrar_##HandlerType({__VA_ARGS__});
