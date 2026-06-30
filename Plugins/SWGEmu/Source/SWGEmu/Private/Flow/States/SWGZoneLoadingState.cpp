#include "Flow/States/SWGZoneLoadingState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

void FSWGZoneLoadingState::Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload)
{
	TSharedPtr<FSWGSceneStartPayload> ScenePayload = StaticCastSharedPtr<FSWGSceneStartPayload>(Payload);
	if (!ScenePayload.IsValid() || !ScenePayload->Scene.IsValid())
	{
		UIStateMachine.Fail(TEXT("ZoneLoading entered without a scene to load"));
		return;
	}

	const FCmdStartSceneMessage& Scene = *ScenePayload->Scene;

	// TODO: kick off the level/terrain load for Scene.TerrainName and place the
	// player at (Scene.PosX, Scene.PosY, Scene.PosZ). Once the level is ready,
	// send CmdSceneReadyMessage and transition to InWorld.
}

void FSWGZoneLoadingState::Exit(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}

REGISTER_FLOW_STATE(FSWGZoneLoadingState, ESWGClientState::ZoneLoading)
