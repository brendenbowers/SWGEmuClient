#pragma once
#include "Flow/SWGFlowState.h"
#include "Network/Messages/Zone/CmdStartSceneMessage.h"
#include "UObject/WeakObjectPtr.h"

class USWGObjectGraphSubsystem;

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

private:
	void OnReadyStatesChanged(USWGClientFlowSubsystem& UIStateMachine, const FSWGFlowContext& Ctx, const int32 Epoch);

	TWeakObjectPtr<USWGObjectGraphSubsystem> ObjectGraphWeak;
	FDelegateHandle ZoneRevealedHandle;
	FDelegateHandle TerrainReadyHandle;

	/** One-shot: defers BeginLoadTerrain until the OpenLevel travel actually finishes loading the new world. */
	FDelegateHandle PostLoadMapHandle;

	// Set in Enter(), read back when PostLoadMapHandle fires. Stored as plain
	// member state rather than captured into the PostLoadMapWithWorld lambda —
	// a captured FString comes back with corrupted character data after a full
	// OpenLevel world travel, unlike ordinary member fields. Root cause unknown.
	FString PendingTerrainName;
	FVector PendingSpawnPosition = FVector::ZeroVector;
};
