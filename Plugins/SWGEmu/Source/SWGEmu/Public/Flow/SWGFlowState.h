#pragma once

#include "CoreMinimal.h"
#include "Flow/SWGFlowContext.h"

class USWGClientFlowSubsystem;

/** Interface every state class implements. */
class ISWGFlowState
{
public:
	virtual ~ISWGFlowState() = default;

	virtual void Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}
	virtual void Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}
	virtual void Tick (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, float Dt) {}
};
