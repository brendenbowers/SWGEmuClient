#pragma once

#include "CoreMinimal.h"
#include "Flow/SWGClientState.h"

class ISWGFlowState;
class USWGClientFlowSubsystem;

/**
 * Central registry for state machine states. States are defined as factory functions
 * at module startup. When a flow subsystem initializes, the registry creates fresh
 * state instances via these factories and registers them with the subsystem.
 */
class SWGEMU_API FSWGFlowStateRegistry
{
public:
	static FSWGFlowStateRegistry& Get();

	/** Register a state factory. Called at module startup. */
	void RegisterStateDefinition(ESWGClientState StateType, TFunction<TSharedPtr<ISWGFlowState>()> Factory, ESWGClientState PreviousState = ESWGClientState::None);

	/** Create and register all known states with a subsystem. Called during subsystem initialization. */
	void RegisterAllStates(USWGClientFlowSubsystem& Subsystem);

private:
	FSWGFlowStateRegistry() = default;

	struct FStateDefinition
	{
		ESWGClientState StateType;
		ESWGClientState PreviousState;
		TFunction<TSharedPtr<ISWGFlowState>()> Factory;
	};

	TArray<FStateDefinition> StateDefinitions;
};
