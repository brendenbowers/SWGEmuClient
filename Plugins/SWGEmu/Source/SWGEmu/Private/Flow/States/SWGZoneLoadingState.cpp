#include "Flow/States/SWGZoneLoadingState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Kismet/GameplayStatics.h"

void FSWGZoneLoadingState::Enter(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx, const TSharedPtr<FSWGTransitionPayload>& Payload)
{
	TSharedPtr<FSWGSceneStartPayload> ScenePayload = StaticCastSharedPtr<FSWGSceneStartPayload>(Payload);
	if (!ScenePayload.IsValid() || !ScenePayload->Scene.IsValid())
	{
		UIStateMachine.Fail(TEXT("ZoneLoading entered without a scene to load"));
		return;
	}

	const FCmdStartSceneMessage& Scene = *ScenePayload->Scene;

	UWorld* World = UIStateMachine.GetWorld();
	UGameplayStatics::OpenLevel(World, TEXT("SWGLevel"));

	


}

void FSWGZoneLoadingState::Exit(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx) {}

REGISTER_FLOW_STATE(FSWGZoneLoadingState, ESWGClientState::ZoneLoading)
