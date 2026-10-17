#include "Subsystems/SWGEffectSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTargetSubsystem.h"
#include "Common/SWGWorldScale.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "SWGLogCategories.h"

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
}

static FAutoConsoleCommandWithWorldAndArgs GEffectPlayOnTargetCommand(
	TEXT("swg.Effect.PlayOnTarget"),
	TEXT("swg.Effect.PlayOnTarget <cef> [hardpoint] - plays a clienteffect on the current target, or on the player without one."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = FindRunningGameInstance(World);
		USWGEffectSubsystem* Effects = GameInstance ? GameInstance->GetSubsystem<USWGEffectSubsystem>() : nullptr;
		if (!Effects || Args.IsEmpty())
		{
			UE_LOG(LogSWGEffect, Warning, TEXT("Usage: swg.Effect.PlayOnTarget <cef> [hardpoint] (needs a running game)"));
			return;
		}
		const USWGTargetSubsystem* Targeting = GameInstance->GetSubsystem<USWGTargetSubsystem>();
		const USWGObjectGraphSubsystem* ObjectGraph = GameInstance->GetSubsystem<USWGObjectGraphSubsystem>();
		int64 ObjectId = Targeting ? Targeting->GetTargetId() : 0;
		if (ObjectId == 0 && ObjectGraph)
		{
			ObjectId = ObjectGraph->GetLocalPlayerObjectId();
		}
		const int32 Started = Effects->PlayEffectOnObject(Args[0], ObjectId, Args.Num() > 1 ? Args[1] : FString());
		UE_LOG(LogSWGEffect, Warning, TEXT("swg.Effect.PlayOnTarget: %d sound(s) on %lld"), Started, ObjectId);
	}));

static FAutoConsoleCommandWithWorldAndArgs GEffectPlayAtPlayerCommand(
	TEXT("swg.Effect.PlayAtPlayer"),
	TEXT("swg.Effect.PlayAtPlayer <cef> - plays a clienteffect at the player's position through the location path."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = FindRunningGameInstance(World);
		USWGEffectSubsystem* Effects = GameInstance ? GameInstance->GetSubsystem<USWGEffectSubsystem>() : nullptr;
		const APlayerController* Controller = GameInstance ? GameInstance->GetFirstLocalPlayerController() : nullptr;
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!Effects || !Pawn || Args.IsEmpty())
		{
			UE_LOG(LogSWGEffect, Warning, TEXT("Usage: swg.Effect.PlayAtPlayer <cef> (needs a running game with a pawn)"));
			return;
		}
		const int32 Started = Effects->PlayEffectAtLocation(Args[0], FString(), SWGToRawSpace(Pawn->GetActorLocation()), 0);
		UE_LOG(LogSWGEffect, Warning, TEXT("swg.Effect.PlayAtPlayer: %d sound(s)"), Started);
	}));

#endif // !UE_BUILD_SHIPPING
