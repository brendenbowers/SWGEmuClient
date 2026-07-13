#pragma once

#include "CoreMinimal.h"
#include "Flow/SWGFlowContext.h"

class USWGClientFlowSubsystem;

/**
 * Base for transient, one-shot data handed from one state to the next via
 * TransitionTo. Unlike FSWGFlowContext (the persistent blackboard), a payload
 * lives only for the duration of the receiving state's Enter call — use it for
 * data that belongs to a single handoff and should not linger in session state.
 */
struct FSWGTransitionPayload
{
	virtual ~FSWGTransitionPayload() = default;
};

/** Interface every state class implements. */
class ISWGFlowState
{
public:
	virtual ~ISWGFlowState() = default;

	virtual void Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload) {}
	virtual void Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}
	virtual void Tick (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, float Dt) {}
};

/**
 * Register a state with the flow state registry.
 * Use in a .cpp file after the state class definition:
 *   REGISTER_FLOW_STATE(FSWGMyState, ESWGClientState::MyState);
 */
#define REGISTER_FLOW_STATE(StateClass, StateType) \
	REGISTER_FLOW_STATE_WITH_PREVIOUS(StateClass, StateType, ESWGClientState::None)

/**
 * Register a state with a specific previous state constraint.
 * Use in a .cpp file after the state class definition:
 *   REGISTER_FLOW_STATE_WITH_PREVIOUS(FSWGMyState, ESWGClientState::MyState, ESWGClientState::PreviousState);
 *
 * NOTE: The cpp file must include "Flow/SWGFlowStateRegistry.h"
 */
#define REGISTER_FLOW_STATE_WITH_PREVIOUS(StateClass, StateType, PreviousState) \
	class FRegister_##StateClass \
	{ \
	public: \
		FRegister_##StateClass(); \
	}; \
	static FRegister_##StateClass GRegister_##StateClass; \
	inline FRegister_##StateClass::FRegister_##StateClass() \
	{ \
		FSWGFlowStateRegistry::Get().RegisterStateDefinition(StateType, \
			[]() { return MakeShared<StateClass>(); }, PreviousState); \
	}
