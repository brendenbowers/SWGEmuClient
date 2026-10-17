#include "Components/SWGCreatureSoundComponent.h"
#include "Objects/Creature/SWGCreature.h"
#include "SWGAudioTypes.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTargetSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace
{
	// From the editor's console the world is the editor's; use the running game's instead.
	UGameInstance* FindCreatureSoundGameInstance(UWorld* World)
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

	/** The current target's sound component, or the local player's without a target. */
	USWGCreatureSoundComponent* FindSoundComponent(UWorld* World)
	{
		UGameInstance* GameInstance = FindCreatureSoundGameInstance(World);
		const USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
		if (!ObjectGraph)
		{
			return nullptr;
		}

		const USWGTargetSubsystem* Targeting = GameInstance->GetSubsystem<USWGTargetSubsystem>();
		int64 ObjectId = Targeting ? Targeting->GetTargetId() : 0;
		if (ObjectId == 0)
		{
			ObjectId = ObjectGraph->GetLocalPlayerObjectId();
		}
		const ASWGCreature* Creature = Cast<ASWGCreature>(ObjectGraph->FindActor(ObjectId));
		return Creature ? Creature->CreatureSoundComponent.Get() : nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAudioEventCommand(
	TEXT("swg.Audio.Event"),
	TEXT("swg.Audio.Event <event> - fires an animation sound event (event_footstep, vocalize, hitlight, sound_<name>, ...) on the target, or on the player without one."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGCreatureSoundComponent* Sound = FindSoundComponent(World);
		if (!Sound || Args.IsEmpty())
		{
			UE_LOG(LogSWGAudio, Warning, TEXT("Usage: swg.Audio.Event <event> (needs a running game with a player or target creature)"));
			return;
		}
		const bool bStarted = Sound->FireEvent(FName(*Args[0]));
		UE_LOG(LogSWGAudio, Warning, TEXT("swg.Audio.Event %s on %s: %s"), *Args[0], *GetNameSafe(Sound->GetOwner()), bStarted ? TEXT("started") : TEXT("nothing played (no cdf entry, out of range, or rate limited)"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GAudioSurfaceCommand(
	TEXT("swg.Audio.Surface"),
	TEXT("swg.Audio.Surface - prints the footstep surface under the target, or under the player without one."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGCreatureSoundComponent* Sound = FindSoundComponent(World);
		if (!Sound)
		{
			UE_LOG(LogSWGAudio, Warning, TEXT("swg.Audio.Surface: needs a running game with a player or target creature"));
			return;
		}
		const FString Surface = Sound->ResolveSurface();
		UE_LOG(LogSWGAudio, Warning, TEXT("swg.Audio.Surface: %s stands on '%s'"), *GetNameSafe(Sound->GetOwner()), Surface.IsEmpty() ? TEXT("(unknown)") : *Surface);
	}));

#endif
