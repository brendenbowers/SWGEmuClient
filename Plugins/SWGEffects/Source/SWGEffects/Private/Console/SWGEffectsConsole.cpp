#include "SWGEffectsSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "SWGEffects.h"

#if !UE_BUILD_SHIPPING

namespace
{
	// From the editor's console the world is the editor's; use the running game's instead.
	UGameInstance* FindRunningGameInstance(UWorld* World)
	{
		if (World && World->GetGameInstance())
		{
			return World->GetGameInstance();
		}
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.OwningGameInstance)
				{
					return Context.OwningGameInstance;
				}
			}
		}
		return nullptr;
	}

	USWGEffectsSubsystem* FindEffects(UWorld* World)
	{
		const UGameInstance* GameInstance = FindRunningGameInstance(World);
		UWorld* GameWorld = GameInstance ? GameInstance->GetWorld() : nullptr;
		return GameWorld ? GameWorld->GetSubsystem<USWGEffectsSubsystem>() : nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GFxPlayCommand(
	TEXT("swg.Fx.Play"),
	TEXT("swg.Fx.Play <prt> [distanceMetres] - plays a .prt effect in front of the player (default 5 m). Bare names resolve under appearance/."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGEffectsSubsystem* Effects = FindEffects(World);
		if (!Effects || Args.IsEmpty())
		{
			UE_LOG(LogSWGFx, Warning, TEXT("Usage: swg.Fx.Play <prt> [distanceMetres] (needs a running game)"));
			return;
		}

		const float DistanceMeters = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 5.f;
		const UWorld* GameWorld = Effects->GetWorld();
		const APlayerController* Controller = UGameplayStatics::GetPlayerController(GameWorld, 0);
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		FVector Origin = FVector::ZeroVector;
		FRotator Facing = FRotator::ZeroRotator;
		if (Controller && Controller->PlayerCameraManager)
		{
			Origin = Pawn ? Pawn->GetActorLocation() : Controller->PlayerCameraManager->GetCameraLocation();
			Facing = FRotator(0.f, Controller->PlayerCameraManager->GetCameraRotation().Yaw, 0.f);
		}

		const FVector Location = Origin + Facing.Vector() * DistanceMeters * 100.f;
		const FSWGEffectHandle Handle = Effects->PlayParticleAtLocation(Args[0], FTransform(Facing, Location));
		UE_LOG(LogSWGFx, Warning, TEXT("swg.Fx.Play %s at %s: %s"), *Args[0], *Location.ToCompactString(), Handle.IsValid() ? TEXT("started") : TEXT("not started (missing, culled or over budget)"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GFxStatsCommand(
	TEXT("swg.Fx.Stats"),
	TEXT("swg.Fx.Stats - active effects, live particles, pools and caches."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		const USWGEffectsSubsystem* Effects = FindEffects(World);
		if (!Effects)
		{
			UE_LOG(LogSWGFx, Warning, TEXT("swg.Fx.Stats: needs a running game"));
			return;
		}
		const FSWGEffectsStats Stats = Effects->GetStats();
		UE_LOG(LogSWGFx, Warning, TEXT("Fx: %d active (%d culled), %d live particles, pooled %d effects / %d instance comps / %d mesh comps, cached %d effects / %d materials, %d plays rejected"),
			Stats.ActiveEffects, Stats.CulledEffects, Stats.LiveParticles, Stats.PooledEffects, Stats.PooledInstanceComponents, Stats.PooledMeshComponents,
			Stats.CachedEffects, Stats.CachedMaterials, Stats.RejectedPlays);
	}));

static FAutoConsoleCommandWithWorldAndArgs GFxStopAllCommand(
	TEXT("swg.Fx.StopAll"),
	TEXT("swg.Fx.StopAll - removes every playing particle effect immediately."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		if (USWGEffectsSubsystem* Effects = FindEffects(World))
		{
			Effects->StopAll(true);
			UE_LOG(LogSWGFx, Warning, TEXT("swg.Fx.StopAll: stopped"));
		}
	}));

#endif // !UE_BUILD_SHIPPING
