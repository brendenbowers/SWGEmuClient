#pragma once
#include "Flow/SWGFlowState.h"

class FSWGGalaxySelectedState : public ISWGFlowState
{
public:
	virtual void Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload) override;
	virtual void Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) override;
};
