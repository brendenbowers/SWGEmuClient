#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGFourCC.h"

class AActor;
struct FBaselinesMessage;
class USWGObjectGraphSubsystem;

/**
 * Additional Context the Baseline Hanlder can use to handle baselines
 */
struct FSWGBaselineArguments
{
	USWGObjectGraphSubsystem* ObjectGraph = nullptr;
};

/**
 * Contract to define a class that will handle baselines, should be registered with the 
 * REGISTER_SWG_BASELINE_HANDLER macro
 */
class ISWGBaselineHandler
{
public:
	virtual ~ISWGBaselineHandler() = default;

	/**
	 * Once a macro is resolvedby its ESWGObjectType, this check is called to see if the handler can handle the message
	 */
	virtual bool CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const = 0;

	/**
	 * Handles the baseline message by processing and applying it to the given actor. Rreturn false to allow another handler to process the request
	 * or if it cant be handled. NB: The CanHandleBaseline should be accurate on if this can handle the request. 
	 */
	virtual bool HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args) = 0;
};

using FSWGBaselineHandlerFactory = TFunction<TSharedPtr<ISWGBaselineHandler>()>;

/**
 * Registery to resolve baseline handlers.
 */
class SWGEMUCLIENT_API FSWGBaselineHandlerRegistry
{
public:
	static FSWGBaselineHandlerRegistry& Get();

	/**
	 * Registers one handler for every objet type in the list
	 */
	void Register(TArray<ESWGObjectType> ObjectTypes, FSWGBaselineHandlerFactory&& Factory);

	/**
	 * Resolves the handlers registered for the message type and runs until a handler that claims the message completes successfuly
	 */
	bool TryHandle(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args) const;

	/** Determines if a hanlder can handle the the message */
	bool HasHandlerFor(const AActor& Actor, const FBaselinesMessage& Msg) const;

	/** Returnes true if any handlers exist for the object type */
	bool IsRegistered(ESWGObjectType ObjectType) const { return HandlerIndicesByType.Contains(ObjectType); }

private:
	struct FEntry
	{
		FSWGBaselineHandlerFactory Factory;
		mutable TSharedPtr<ISWGBaselineHandler> Handler;
	};

	static ISWGBaselineHandler* ResolveHandler(const FEntry& Entry);

	TArray<FEntry> Entries;

	TMap<ESWGObjectType, TArray<int32>> HandlerIndicesByType;
};

template<typename THandler>
struct TSWGBaselineHandlerRegistrar
{
	explicit TSWGBaselineHandlerRegistrar(std::initializer_list<ESWGObjectType> ObjectTypes)
	{
		FSWGBaselineHandlerRegistry::Get().Register(TArray<ESWGObjectType>(ObjectTypes), []() -> TSharedPtr<ISWGBaselineHandler>
		{
			return MakeShared<THandler>();
		});
	}
};

/**
 * Use this in a handler's .cpp file to register as the baseline
 * handler for the given object types.
 *
 * Example:
 *   REGISTER_SWG_BASELINE_HANDLER(FSWGTangibleBaselineHandler,
 *       ESWGObjectType::TANO, ESWGObjectType::WEAO, ESWGObjectType::RCNO)
 */
#define REGISTER_SWG_BASELINE_HANDLER(HandlerType, ...) \
	static TSWGBaselineHandlerRegistrar<HandlerType> GBaselineRegistrar_##HandlerType({__VA_ARGS__});
