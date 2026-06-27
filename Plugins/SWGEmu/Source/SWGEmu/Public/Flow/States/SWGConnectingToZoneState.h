#pragma once
#include "Flow/SWGFlowState.h"

class FSWGConnectingToZoneState : public ISWGFlowState
{
public:
	virtual void Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) override;
	virtual void Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) override;
};
