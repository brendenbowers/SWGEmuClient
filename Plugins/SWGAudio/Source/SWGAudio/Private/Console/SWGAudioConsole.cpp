#include "SWGAudioSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "SWGAudioTypes.h"
#include "SWGMusicManager.h"
#include "UObject/UObjectIterator.h"

#if !UE_BUILD_SHIPPING

namespace
{
	// Resolved at invocation, not captured: a console command outlives the subsystem.
	USWGAudioSubsystem* FindLiveAudioSubsystem()
	{
		for (TObjectIterator<USWGAudioSubsystem> It; It; ++It)
		{
			if (IsValid(*It) && It->GetGameInstance())
			{
				return *It;
			}
		}
		return nullptr;
	}

	FString DescribeHandles(const TArray<FSWGSoundHandle>& Handles)
	{
		int32 Started = 0;
		for (const FSWGSoundHandle& Handle : Handles)
		{
			Started += Handle.IsValid() ? 1 : 0;
		}
		return FString::Printf(TEXT("%d of %d sounds started"), Started, Handles.Num());
	}
}

static FAutoConsoleCommand SWGAudioPlayCmd(
	TEXT("swg.Audio.Play"),
	TEXT("swg.Audio.Play <snd> - plays a .snd (\"ui_rollover\", \"sound/ui_rollover.snd\"); 3D sounds play at the player's pawn, others 2D."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogSWGAudio, Warning, TEXT("Usage: swg.Audio.Play <snd>"));
				return;
			}

			USWGAudioSubsystem* Audio = FindLiveAudioSubsystem();
			if (!Audio)
			{
				UE_LOG(LogSWGAudio, Warning, TEXT("swg.Audio.Play: no live audio subsystem"));
				return;
			}

			FSWGSoundHandle Handle;
			const APawn* Pawn = nullptr;
			if (const UWorld* World = Audio->GetWorld())
			{
				if (const APlayerController* Controller = World->GetFirstPlayerController())
				{
					Pawn = Controller->GetPawn();
				}
			}

			const TSharedPtr<const FSWGSoundTemplate> Template = Audio->GetSoundTemplate(SWGAudio::NormalizeSoundPath(Args[0]));
			if (Template.IsValid() && Template->b3D && Pawn)
			{
				Handle = Audio->PlaySoundAtLocation(Args[0], Pawn->GetActorLocation());
			}
			else
			{
				Handle = Audio->PlaySound2D(Args[0]);
			}

			UE_LOG(LogSWGAudio, Display, TEXT("swg.Audio.Play %s: %s"), *Args[0], Handle.IsValid() ? TEXT("started") : TEXT("could not start (see warnings)"));
		}));

static FAutoConsoleCommand SWGAudioPlayCefCmd(
	TEXT("swg.Audio.PlayCef"),
	TEXT("swg.Audio.PlayCef <cef> - plays every sound of a clienteffect (\"clienteffect/door_open.cef\" or the bare name)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogSWGAudio, Warning, TEXT("Usage: swg.Audio.PlayCef <cef>"));
				return;
			}

			USWGAudioSubsystem* Audio = FindLiveAudioSubsystem();
			if (!Audio)
			{
				UE_LOG(LogSWGAudio, Warning, TEXT("swg.Audio.PlayCef: no live audio subsystem"));
				return;
			}

			const TArray<FSWGSoundHandle> Handles = Audio->PlayClientEffectSounds(Args[0]);
			UE_LOG(LogSWGAudio, Display, TEXT("swg.Audio.PlayCef %s: %s"), *Args[0], *DescribeHandles(Handles));
		}));

static FAutoConsoleCommand SWGAudioStopAllCmd(
	TEXT("swg.Audio.StopAll"),
	TEXT("swg.Audio.StopAll [fadeSeconds] - stops every playing sound."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (USWGAudioSubsystem* Audio = FindLiveAudioSubsystem())
			{
				Audio->StopAll(Args.Num() >= 1 ? FCString::Atof(*Args[0]) : 0.f);
			}
		}));

static FAutoConsoleCommand SWGAudioStatsCmd(
	TEXT("swg.Audio.Stats"),
	TEXT("swg.Audio.Stats - active sound instances per category, PCM cache size and hit rate."),
	FConsoleCommandDelegate::CreateLambda([]()
		{
			USWGAudioSubsystem* Audio = FindLiveAudioSubsystem();
			if (!Audio)
			{
				UE_LOG(LogSWGAudio, Warning, TEXT("swg.Audio.Stats: no live audio subsystem"));
				return;
			}

			const FSWGAudioStats Stats = Audio->GetStats();
			FString Categories;
			for (const TPair<int32, int32>& Pair : Stats.ActiveByCategory)
			{
				Categories += FString::Printf(TEXT(" cat%d=%d"), Pair.Key, Pair.Value);
			}
			UE_LOG(LogSWGAudio, Display, TEXT("Audio: %d active instances (%s )"), Stats.ActiveInstances, *Categories);
			UE_LOG(LogSWGAudio, Display, TEXT("PCM cache: %d samples, %.1f / %.1f MB, hits %d, misses %d, pending decodes %d; templates cached %d"),
				Stats.CachedSamples, Stats.CacheBytes / 1048576.0, Stats.BudgetBytes / 1048576.0, Stats.CacheHits, Stats.CacheMisses,
				Stats.PendingDecodes, Stats.CachedTemplates);
		}));

static FAutoConsoleCommand SWGAudioMusicCmd(
	TEXT("swg.Audio.Music"),
	TEXT("swg.Audio.Music <snd|none> [server|combat|environment] - sets a music layer (default server); \"none\" clears it."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			USWGAudioSubsystem* Audio = FindLiveAudioSubsystem();
			USWGMusicManager* Music = Audio ? Audio->GetMusicManager() : nullptr;
			if (Args.Num() < 1 || !Music)
			{
				UE_LOG(LogSWGAudio, Warning, TEXT("Usage: swg.Audio.Music <snd|none> [server|combat|environment] (needs a live audio subsystem)"));
				return;
			}

			ESWGMusicLayer Layer = ESWGMusicLayer::Server;
			if (Args.Num() >= 2)
			{
				if (Args[1].Equals(TEXT("combat"), ESearchCase::IgnoreCase))
				{
					Layer = ESWGMusicLayer::Combat;
				}
				else if (Args[1].Equals(TEXT("environment"), ESearchCase::IgnoreCase))
				{
					Layer = ESWGMusicLayer::Environment;
				}
			}
			Music->SetLayer(Layer, Args[0]);
			UE_LOG(LogSWGAudio, Display, TEXT("swg.Audio.Music: layer %d = '%s'"), static_cast<int32>(Layer), *Music->GetLayerPath(Layer));
		}));

#endif // !UE_BUILD_SHIPPING
