#include "Flow/States/SWGZoneLoadingState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const FName ZoneLevelName(TEXT("SWGLevel"));
}

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
	UGameplayStatics::OpenLevel(World, ZoneLevelName);
	//UGameplayStatics::LoadStreamLevel(World, ZoneLevelName, false, false, FLatentActionInfo());

	// LoadStreamLevel finds-or-creates the ULevelStreaming entry and returns
	// immediately (async load, no completion callback wired here yet) — the
	// streaming object exists right away, but GetLoadedLevel() on it stays
	// null until the load actually finishes. USWGObjectGraphSubsystem's
	// GetSpawnLevel() resolves that lazily at spawn time, so it's safe to
	// hand over the streaming pointer now rather than waiting on completion.
	//ULevelStreaming* ZoneStreamingLevel = UGameplayStatics::GetStreamingLevel(World, ZoneLevelName);

	if (UGameInstance* GameInstance = UIStateMachine.GetGameInstance())
	{
		if (USWGObjectGraphSubsystem* ObjectGraph = GameInstance->GetSubsystem<USWGObjectGraphSubsystem>())
		{

			const int32 Epoch = UIStateMachine.Epoch;
			TWeakObjectPtr<USWGClientFlowSubsystem> StateMachineWeak = &UIStateMachine;

			// There's no "all baselines received" moment for a zone — objects keep
			// streaming in as you move around — so the local player's own
			// SceneEndBaselines (which is what actually flips the level visible,
			// see USWGObjectGraphSubsystem::HandleSceneEndBaselines) is the signal
			// that the world is actually ready to look at.
			ZoneRevealedHandle = ObjectGraph->OnZoneLevelRevealed.AddLambda([StateMachineWeak, Epoch]()
				{
					UE_LOG(LogTemp, Log, TEXT("FSWGZoneLoadingState: zone level revealed, transitioning to InWorld state"));
					USWGClientFlowSubsystem* StateMachine = StateMachineWeak.Get();
					if (!StateMachine || StateMachine->Epoch != Epoch)
					{
						UE_LOG(LogTemp, Warning, TEXT("FSWGZoneLoadingState: Epoch mismatch during zone reveal handling. Expected %d, got %d"), Epoch, StateMachine ? StateMachine->Epoch : -1);
						return;
					}

					StateMachine->TransitionTo(ESWGClientState::InWorld);
				});

			//ObjectGraph->SetCurrentZoneLevel(ZoneStreamingLevel);

			ObjectGraphWeak = ObjectGraph;


		}
	}
}

void FSWGZoneLoadingState::Exit(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx)
{
	if (USWGObjectGraphSubsystem* ObjectGraph = ObjectGraphWeak.Get())
	{
		if (ZoneRevealedHandle.IsValid())
		{
			ObjectGraph->OnZoneLevelRevealed.Remove(ZoneRevealedHandle);
		}
	}

	ZoneRevealedHandle.Reset();
	ObjectGraphWeak.Reset();
}

REGISTER_FLOW_STATE(FSWGZoneLoadingState, ESWGClientState::ZoneLoading)
