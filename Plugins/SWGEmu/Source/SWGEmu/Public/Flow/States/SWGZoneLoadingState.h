#pragma once
#include "Flow/SWGFlowState.h"
#include "Network/Messages/Zone/CmdStartSceneMessage.h"

/**
 * Transient handoff from CharacterSelected carrying the scene data from CmdStartScene.
 * Consumed by ZoneLoadingState::Enter; never stored in the persistent FSWGFlowContext.
 */
struct FSWGSceneStartPayload : public FSWGTransitionPayload
{
	TSharedPtr<const FCmdStartSceneMessage> Scene;

	explicit FSWGSceneStartPayload(TSharedPtr<const FCmdStartSceneMessage> InScene)
		: Scene(MoveTemp(InScene)) {}
};

class FSWGZoneLoadingState : public ISWGFlowState
{
public:
	virtual void Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload) override;
	virtual void Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) override;
};
