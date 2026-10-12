#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGPerformanceAudioSubsystem.h"
#include "SWGAudioTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace
{
	// From the editor's console the world is the editor's; use the running game's instead.
	UGameInstance* FindPerformanceGameInstance(UWorld* World)
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

static FAutoConsoleCommandWithWorldAndArgs GAudioPerformCommand(
	TEXT("swg.Audio.Perform"),
	TEXT("swg.Audio.Perform <song> <instrument|instrumentId> [flourish 1-8] | <row index> [flourish] | stop (outro) | off - plays entertainer music on the player without a server."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GameInstance = FindPerformanceGameInstance(World);
		USWGPerformanceAudioSubsystem* Performance = GameInstance ? GameInstance->GetSubsystem<USWGPerformanceAudioSubsystem>() : nullptr;
		const USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
		AActor* Player = ObjectGraph ? ObjectGraph->FindActor(ObjectGraph->GetLocalPlayerObjectId()) : nullptr;
		const FSWGPerformanceTable* Table = Performance ? Performance->GetTable() : nullptr;
		if (!Table || !Player || Args.IsEmpty())
		{
			UE_LOG(LogSWGAudio, Warning, TEXT("Usage: swg.Audio.Perform <song> <instrument|id> [flourish] | <row index> [flourish] | stop | off (needs a running game with a player)"));
			return;
		}

		if (Args[0] == TEXT("stop") || Args[0] == TEXT("off"))
		{
			if (Args[0] == TEXT("stop"))
			{
				Performance->PerformFlourish(Player, -1);
			}
			else
			{
				Performance->SetPerformance(Player, 0);
			}
			return;
		}

		// A lone number is the performance.iff row index, as the server sends it.
		int32 PerformanceIndex = 0;
		int32 FlourishId = 0;
		if (Args.Num() == 1 || (Args.Num() == 2 && Args[0].IsNumeric()))
		{
			PerformanceIndex = FCString::Atoi(*Args[0]);
			FlourishId = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 0;
		}
		else
		{
			const int32 InstrumentId = Args[1].IsNumeric() ? FCString::Atoi(*Args[1]) : Table->FindInstrumentAudioId(Args[1]);
			const FSWGPerformanceRow* Song = Table->FindSong(Args[0], InstrumentId);
			PerformanceIndex = Song ? Song->Index : 0;
			FlourishId = Args.Num() > 2 ? FCString::Atoi(*Args[2]) : 0;
		}

		if (!Table->FindByIndex(PerformanceIndex))
		{
			UE_LOG(LogSWGAudio, Warning, TEXT("swg.Audio.Perform: no performance.iff row for these arguments"));
			return;
		}

		Performance->SetPerformance(Player, PerformanceIndex);
		if (FlourishId != 0)
		{
			Performance->PerformFlourish(Player, FlourishId);
		}
		const FSWGPerformanceRow& Row = *Table->FindByIndex(PerformanceIndex);
		UE_LOG(LogSWGAudio, Warning, TEXT("swg.Audio.Perform: row %d %s on %s (%s), loop %.2fs, flourish %d"),
			Row.Index, *Row.Name, *Row.RequiredInstrument, *Row.MainLoop, Row.LoopDurationSeconds, FlourishId);
	}));

#endif // !UE_BUILD_SHIPPING
