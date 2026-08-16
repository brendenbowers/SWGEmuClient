#pragma once
#include "Flow/SWGFlowState.h"

class FSWGCharacterSelectState : public ISWGFlowState
{
public:
	virtual void Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload) override;
	virtual void Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) override;
	virtual void Tick(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, float Dt) override;

private:
	int64 SelectedCharacterID = -1;
	void DisplpayCharacter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx);
};
