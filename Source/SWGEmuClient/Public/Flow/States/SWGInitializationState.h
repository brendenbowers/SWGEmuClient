#pragma once
#include "Flow/SWGFlowState.h"

/**
 * Runs once at boot, before the connection screen (ESWGClientState::Disconnected)
 * is shown. Currently just confirms USWGTreSubsystem finished loading the TRE
 * archives/CRC table (it auto-loads from DefaultGame.ini on GameInstance
 * Initialize, well before this state's Enter() runs) and logs/broadcasts
 * accordingly. Synchronous today since LoadArchives() is synchronous — if TRE
 * loading ever becomes async, this is the natural place to Tick() until ready
 * instead of transitioning immediately.
 */
class FSWGInitializationState : public ISWGFlowState
{
public:
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu")
	TObjectPtr<UDataTable> FormTagMappingTable;

	virtual void Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload) override;
	virtual void Exit (USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) override;
};
