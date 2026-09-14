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

private:
	/**
	 * One-time setup for the character-select preview backdrop: loads
	 * texture/ui_background_arrow.dds (see ui/ui_backdrop_default.inc's
	 * SourceResource='ui_background_arrow' binding) via USWGTreSubsystem::
	 * GetOrLoadTexture, wraps it in a plain unlit material (M_UIBackdropUnlit),
	 * and applies it to the CharacterPreviewBackdrop-tagged actor in
	 * L_Startup. Runs once here rather than every time the character-select
	 * state is entered, since neither the texture nor the actor change.
	 */
	void SetupPreviewBackdrop(UGameInstance& GameInstance);
};
