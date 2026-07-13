#include "Flow/States/SWGZoneLoadingState.h"
#include "Flow/SWGFlowStateRegistry.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Subsystems/SWGTerrainSubsystem.h"

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

	if (UIStateMachine.TerrainSubsystem)
	{
		TWeakObjectPtr<USWGClientFlowSubsystem> StateMachineWeak = &UIStateMachine;
		const int32 Epoch = UIStateMachine.Epoch;
		TWeakObjectPtr<USWGTerrainSubsystem> TerrainSubsystemWeak = UIStateMachine.TerrainSubsystem;
		TFunction<void()> OnTerrainReadyCallback = [this, StateMachineWeak, TerrainSubsystemWeak, &Ctx, Epoch]()
			{
				USWGClientFlowSubsystem* StateMachine = StateMachineWeak.Get();
				if (!StateMachine || StateMachine->Epoch != Epoch)
				{
					return;
				}
				if (USWGTerrainSubsystem* TerrainSubsystem = TerrainSubsystemWeak.Get())
				{
					TerrainSubsystem->OnTerrainReady.Clear();
				}

				Ctx.bTerrainLoaded = true;
				OnReadyStatesChanged(*StateMachine, Ctx, Epoch);
			};
		TerrainReadyHandle = UIStateMachine.TerrainSubsystem->OnTerrainReady.AddLambda(OnTerrainReadyCallback);

		// BeginLoadTerrain must not run until the OpenLevel travel below finishes
		// loading the new world, or GetWorld() inside the terrain subsystem
		// resolves to the old (pre-travel) level. PostLoadMapWithWorld fires once
		// for the newly-loaded world regardless of travel type — defer until then.
		PendingTerrainName = Scene.TerrainName;
		// No axis swap — Scene.PosX/PosY/PosZ match the server's PositionX/Y/Z
		// directly (the wire's X,Z,Y transmission order is just Core3's send
		// sequence, not a relabeling); SWG already matches UE's X/Y-horizontal,
		// Z-vertical convention 1:1.
		PendingSpawnPosition = FVector(Scene.PosX, Scene.PosY, Scene.PosZ);
		UE_LOG(LogTemp, Log, TEXT("FSWGZoneLoadingState::Enter: PendingTerrainName='%s' (Len=%d)"), *PendingTerrainName, PendingTerrainName.Len());

		PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda(
			[this, StateMachineWeak, TerrainSubsystemWeak, Epoch](UWorld* /*LoadedWorld*/)
			{
				FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
				PostLoadMapHandle.Reset();

				USWGClientFlowSubsystem* StateMachine = StateMachineWeak.Get();
				if (!StateMachine || StateMachine->Epoch != Epoch)
				{
					return;
				}

				UE_LOG(LogTemp, Log, TEXT("FSWGZoneLoadingState: PostLoadMapWithWorld fired, PendingTerrainName='%s' (Len=%d)"), *PendingTerrainName, PendingTerrainName.Len());

				if (USWGTerrainSubsystem* TerrainSubsystem = TerrainSubsystemWeak.Get())
				{
					TerrainSubsystem->BeginLoadTerrain(PendingTerrainName, PendingSpawnPosition);
				}

				// Same reasoning as terrain above, for the object graph's own
				// spawning — see USWGObjectGraphSubsystem::OnZoneLevelLoaded's
				// comment: messages that arrived between CmdStartScene and this
				// point (including the player's own one-time spawn) were being
				// created in the old, about-to-be-destroyed level and destroyed
				// moments later when OpenLevel's deferred travel finally landed.
				if (UGameInstance* GameInstance = StateMachine->GetGameInstance())
				{
					if (USWGObjectGraphSubsystem* ObjectGraph = GameInstance->GetSubsystem<USWGObjectGraphSubsystem>())
					{
						ObjectGraph->OnZoneLevelLoaded();
					}
				}
			});
	}

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
			ZoneRevealedHandle = ObjectGraph->OnZoneLevelRevealed.AddLambda([this, StateMachineWeak, &Ctx, Epoch]()
				{
					UE_LOG(LogTemp, Log, TEXT("FSWGZoneLoadingState: zone level revealed, transitioning to InWorld state"));
					USWGClientFlowSubsystem* StateMachine = StateMachineWeak.Get();
					if (!StateMachine || StateMachine->Epoch != Epoch)
					{
						UE_LOG(LogTemp, Warning, TEXT("FSWGZoneLoadingState: Epoch mismatch during zone reveal handling. Expected %d, got %d"), Epoch, StateMachine ? StateMachine->Epoch : -1);
						return;
					}
					Ctx.bZoneReady = true;
					OnReadyStatesChanged(*StateMachine, Ctx, Epoch);
				});
		}
	}
}

void FSWGZoneLoadingState::Exit(USWGClientFlowSubsystem& UIStateMachine, FSWGFlowContext& Ctx)
{
	if (UGameInstance* GameInstance = UIStateMachine.GetGameInstance())
	{
		if (USWGObjectGraphSubsystem* ObjectGraph = GameInstance->GetSubsystem<USWGObjectGraphSubsystem>())
		{
			ObjectGraph->OnZoneLevelRevealed.Remove(ZoneRevealedHandle);
		}
	}

	if (UIStateMachine.TerrainSubsystem)
	{
		UIStateMachine.TerrainSubsystem->OnTerrainReady.Remove(TerrainReadyHandle);
	}

	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	}

	ZoneRevealedHandle.Reset();
	TerrainReadyHandle.Reset();
	PostLoadMapHandle.Reset();
	ObjectGraphWeak.Reset();
}

void FSWGZoneLoadingState::OnReadyStatesChanged(USWGClientFlowSubsystem& UIStateMachine, const FSWGFlowContext& Ctx, const int32 Epoch)
{
	if (UIStateMachine.Epoch != Epoch)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGZoneLoadingState: Epoch mismatch during OnReadyStatesChanged. Expected %d, got %d"), Epoch, UIStateMachine.Epoch);
		return;
	}

	if(!IsInGameThread())
	{
		AsyncTask(ENamedThreads::GameThread, [this, &UIStateMachine, &Ctx, Epoch]()
			{
				OnReadyStatesChanged(UIStateMachine, Ctx, Epoch);
			});
		return;
	}

	if (Ctx.bTerrainLoaded && Ctx.bZoneReady)
	{
		UIStateMachine.TransitionTo(ESWGClientState::InWorld);
	}
}

REGISTER_FLOW_STATE(FSWGZoneLoadingState, ESWGClientState::ZoneLoading)
