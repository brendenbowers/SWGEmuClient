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

	// Set in Enter(), read back when PostLoadMapHandle fires. Deliberately stored
	// as plain member state rather than captured into the PostLoadMapWithWorld
	// lambda directly — a value captured into that closure was observed coming
	// back with its length intact but its character data corrupted to garbage
	// after a full OpenLevel world travel, while ordinary member fields on this
	// same long-lived instance (Epoch ints, delegate handles, weak pointers) all
	// survive the same window fine. Root cause not fully understood; this
	// sidesteps it rather than explains it.
	FString PendingTerrainName;
	FVector PendingSpawnPosition = FVector::ZeroVector;
};
