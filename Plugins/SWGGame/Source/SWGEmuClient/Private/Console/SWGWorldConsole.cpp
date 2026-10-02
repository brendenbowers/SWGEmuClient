#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGTerrainFloraSubsystem.h"
#include "Components/SWGCombatStateComponent.h"
#include "HAL/IConsoleManager.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectIterator.h"

// Dev: rebuilds the active planet's sky and lights so lighting code changes show without re-zoning.
static FAutoConsoleCommand SWGRelightPlanetCmd(
	TEXT("swg.RelightPlanet"),
	TEXT("Rebuilds the current planet's sun, ambient, fog and sky from its colour ramp."),
	FConsoleCommandDelegate::CreateLambda([]()
		{
			for (TObjectIterator<USWGTerrainSubsystem> It; It; ++It)
			{
				if (IsValid(*It) && It->GetGameInstance())
				{
					It->RelightPlanet();
					return;
				}
			}
			UE_LOG(LogTemp, Warning, TEXT("swg.RelightPlanet: no live terrain subsystem"));
		}));

static FAutoConsoleCommand GSWGFloraStatsCommand(
	TEXT("swg.FloraStats"),
	TEXT("Logs the terrain flora streamer's per-tier cell/instance counts and the flora sample at the player."),
	FConsoleCommandDelegate::CreateLambda([]()
		{
			// Same lookup as swg.Command: the editor console's world isn't the PIE one.
			for (TObjectIterator<USWGTerrainFloraSubsystem> It; It; ++It)
			{
				if (IsValid(*It) && It->GetGameInstance())
				{
					It->DumpStats();
					return;
				}
			}
			UE_LOG(LogTemp, Warning, TEXT("swg.FloraStats: no live flora subsystem — not in a session yet"));
		}));

#if WITH_EDITOR

// Diagnostic — forces the local player's CREO posture without waiting for
// the server to send a delta, so a posture-specific animation bug can be
// reproduced on demand. The posture goes through ApplyPostureUpdate (the
// 0x131 path) so movement follows it too; the state bitmask is written
// directly, which the animation pipeline's per-tick poll still picks up.
static FAutoConsoleCommand SetPostureCmd(
	TEXT("swg.SetPosture"),
	TEXT("swg.SetPosture <postureValue> [stateBitmask] — forces the local player's posture (0=Upright, 1=Crouched, 2=Prone, 8=Sitting, ...) for animation debugging."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogTemp, Warning, TEXT("Usage: swg.SetPosture <postureValue> [stateBitmask]"));
				return;
			}

			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
				{
					World = Context.World();
					break;
				}
			}
			APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
			APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
			USWGCombatStateComponent* CombatState = Pawn ? Pawn->FindComponentByClass<USWGCombatStateComponent>() : nullptr;
			if (!CombatState)
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.SetPosture: no local player with a USWGCombatStateComponent"));
				return;
			}

			if (Args.Num() >= 2)
			{
				CombatState->StateBitmask = FCString::Atoi64(*Args[1]);
			}
			CombatState->ApplyPostureUpdate((uint8)FCString::Atoi(*Args[0]));

			UE_LOG(LogTemp, Warning, TEXT("swg.SetPosture: %s posture=%d states=0x%llx"),
				*Pawn->GetName(), CombatState->Posture, CombatState->StateBitmask);
		}));

#endif

// Dev: what would stop a player walking forward from here. Traces from the
// camera along its view for <distance> (default 500) on the Pawn channel and
// logs every blocking hit's actor, component and mesh asset.
static FAutoConsoleCommand SWGTraceForwardCmd(
	TEXT("swg.TraceForward"),
	TEXT("swg.TraceForward [distance] — logs the collision the camera is looking at, on the Pawn channel."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
				{
					World = Context.World();
					break;
				}
			}
			const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
			if (!PlayerController || !PlayerController->PlayerCameraManager)
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.TraceForward: no player camera"));
				return;
			}

			const float Distance = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 500.0f;
			const FVector Start = PlayerController->PlayerCameraManager->GetCameraLocation();
			const FVector End = Start + PlayerController->PlayerCameraManager->GetCameraRotation().Vector() * Distance;

			TArray<FHitResult> Hits;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(SWGTraceForward), false);
			if (const APawn* Pawn = PlayerController->GetPawn())
			{
				Params.AddIgnoredActor(Pawn);
			}
			World->LineTraceMultiByChannel(Hits, Start, End, ECC_Pawn, Params);

			UE_LOG(LogTemp, Warning, TEXT("swg.TraceForward: %d hit(s) over %.0f from %s"), Hits.Num(), Distance, *Start.ToCompactString());
			for (const FHitResult& Hit : Hits)
			{
				const UPrimitiveComponent* Component = Hit.GetComponent();
				const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
				UE_LOG(LogTemp, Warning, TEXT("   %.0f: %s / %s (%s)%s at %s"),
					Hit.Distance,
					Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("<none>"),
					Component ? *Component->GetName() : TEXT("<none>"),
					Component ? *Component->GetClass()->GetName() : TEXT(""),
					StaticMeshComponent && StaticMeshComponent->GetStaticMesh() ? *FString::Printf(TEXT(" mesh %s"), *StaticMeshComponent->GetStaticMesh()->GetName()) : TEXT(""),
					*Hit.ImpactPoint.ToCompactString());
			}
		}));
