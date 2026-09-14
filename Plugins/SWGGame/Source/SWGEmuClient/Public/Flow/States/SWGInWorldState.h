#pragma once
#include "Flow/SWGFlowState.h"
#include "Common/ResultTypes.h"

struct FSceneEndBaselinesMessage;

class FSWGInWorldState : public ISWGFlowState
{
public:
	virtual void Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload) override;
	virtual void Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) override;
private:
	static void HandleSaveCharacterCache(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, TResult<TSharedPtr<const FSceneEndBaselinesMessage>> Msg);

	/** Watches for a CmdStartScene while in world (teleport, zone change) and goes back through ZoneLoading, which rebuilds everything. */
	FDelegateHandle MessageHandle;
};
