#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "Subsystems/SWGTargetSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWGRadial, Log, All);

// swg.RadialMenu [objectId] — opens the radial menu for the current target
// (or the given object) at screen centre; a right-click without the mouse.
static FAutoConsoleCommandWithWorldAndArgs GSWGRadialMenuCommand(
	TEXT("swg.RadialMenu"),
	TEXT("Request the radial menu for the current target, or for the object id given."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		// The editor console hands over the editor world; the menu lives in the play world.
		if (GEngine && (!World || !World->IsGameWorld()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
					break;
				}
			}
		}

		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGRadialMenuSubsystem* Radial = GameInstance ? GameInstance->GetSubsystem<USWGRadialMenuSubsystem>() : nullptr;
		USWGTargetSubsystem* Targets = GameInstance ? GameInstance->GetSubsystem<USWGTargetSubsystem>() : nullptr;
		if (!Radial)
		{
			return;
		}

		const int64 ObjectId = Args.Num() > 0 ? FCString::Atoi64(*Args[0]) : (Targets ? Targets->GetTargetId() : 0);
		int32 ViewportX = 0, ViewportY = 0;
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			PlayerController->GetViewportSize(ViewportX, ViewportY);
		}
		if (!Radial->RequestMenu(ObjectId, FVector2D(ViewportX * 0.5f, ViewportY * 0.5f)))
		{
			UE_LOG(LogSWGRadial, Warning, TEXT("swg.RadialMenu: no target (pass an object id)"));
		}
	}));

// swg.RadialSelect <radialId> [objectId] — picks an option from the last menu
// received for the target (or given object), as clicking the row would.
static FAutoConsoleCommandWithWorldAndArgs GSWGRadialSelectCommand(
	TEXT("swg.RadialSelect"),
	TEXT("Pick a radial option by id from the last menu received for the current target (or the object id given)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (GEngine && (!World || !World->IsGameWorld()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
					break;
				}
			}
		}

		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGRadialMenuSubsystem* Radial = GameInstance ? GameInstance->GetSubsystem<USWGRadialMenuSubsystem>() : nullptr;
		USWGTargetSubsystem* Targets = GameInstance ? GameInstance->GetSubsystem<USWGTargetSubsystem>() : nullptr;
		if (!Radial || Args.IsEmpty())
		{
			UE_LOG(LogSWGRadial, Warning, TEXT("usage: swg.RadialSelect <radialId> [objectId]"));
			return;
		}

		const int64 ObjectId = Args.Num() > 1 ? FCString::Atoi64(*Args[1]) : (Targets ? Targets->GetTargetId() : 0);
		Radial->SelectOption(ObjectId, FCString::Atoi(*Args[0]));
	}));
