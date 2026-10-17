#include "Objects/World/SWGSoundEmitter.h"
#include "SWGAudioTypes.h"
#include "Subsystems/SWGAmbienceSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

#if !UE_BUILD_SHIPPING

namespace
{
	// Resolved at invocation, not captured: a console command outlives the subsystem.
	USWGAmbienceSubsystem* FindLiveAmbienceSubsystem()
	{
		for (TObjectIterator<USWGAmbienceSubsystem> It; It; ++It)
		{
			if (IsValid(*It) && It->GetGameInstance())
			{
				return *It;
			}
		}
		return nullptr;
	}
}

static FAutoConsoleCommand SWGAmbienceCmd(
	TEXT("swg.Audio.Ambience"),
	TEXT("swg.Audio.Ambience - prints the environment family and row, interior row, reverb, ambient loops and emitter counts."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
		{
			const USWGAmbienceSubsystem* Ambience = FindLiveAmbienceSubsystem();
			if (!Ambience)
			{
				UE_LOG(LogSWGAudio, Warning, TEXT("swg.Audio.Ambience: no live ambience subsystem"));
				return;
			}
			for (const FString& Line : Ambience->DescribeState())
			{
				UE_LOG(LogSWGAudio, Display, TEXT("ambience: %s"), *Line);
			}
		}));

static FAutoConsoleCommand SWGEmittersCmd(
	TEXT("swg.Audio.Emitters"),
	TEXT("swg.Audio.Emitters [meters] - lists sound emitters within meters (default 100) of the camera, nearest first."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const USWGAmbienceSubsystem* Ambience = FindLiveAmbienceSubsystem();
			const UWorld* World = Ambience && Ambience->GetGameInstance() ? Ambience->GetGameInstance()->GetWorld() : nullptr;
			const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
			if (!Controller)
			{
				UE_LOG(LogSWGAudio, Warning, TEXT("swg.Audio.Emitters: no live world"));
				return;
			}

			const float MaxDistance = (Args.Num() > 0 ? FCString::Atof(*Args[0]) : 100.f) * 100.f;
			const FVector Listener = Controller->GetFocalLocation();

			struct FEntry
			{
				float Distance;
				const ASWGSoundEmitter* Emitter;
			};
			TArray<FEntry> Entries;
			for (const TWeakObjectPtr<ASWGSoundEmitter>& Weak : Ambience->GetEmitters())
			{
				if (const ASWGSoundEmitter* Emitter = Weak.Get())
				{
					const float Distance = static_cast<float>(FVector::Dist(Listener, Emitter->GetActorLocation()));
					if (Distance <= MaxDistance)
					{
						Entries.Add({ Distance, Emitter });
					}
				}
			}
			Entries.Sort([](const FEntry& Left, const FEntry& Right) { return Left.Distance < Right.Distance; });

			UE_LOG(LogSWGAudio, Display, TEXT("emitters: %d of %d registered within %.0f m"), Entries.Num(), Ambience->GetEmitters().Num(), MaxDistance / 100.f);
			for (const FEntry& Entry : Entries)
			{
				UE_LOG(LogSWGAudio, Display, TEXT("emitters: %.1f m  %s  (%s)"), Entry.Distance / 100.f, *Entry.Emitter->GetEmitterTemplatePath(), *Entry.Emitter->GetActorLocation().ToString());
				for (const FString& Description : Entry.Emitter->GetLoopDescriptions())
				{
					UE_LOG(LogSWGAudio, Display, TEXT("emitters:     %s"), *Description);
				}
			}
		}));

#endif
