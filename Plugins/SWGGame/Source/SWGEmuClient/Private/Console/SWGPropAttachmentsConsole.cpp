#include "Subsystems/SWGPropAttachmentsSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace
{
	// From the editor's console the world is the editor's; use the running game's instead.
	USWGPropAttachmentsSubsystem* FindPropAttachments(UWorld* World)
	{
		if (World && World->IsGameWorld())
		{
			return World->GetSubsystem<USWGPropAttachmentsSubsystem>();
		}
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
				{
					return Context.World()->GetSubsystem<USWGPropAttachmentsSubsystem>();
				}
			}
		}
		return nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GPropsCommand(
	TEXT("swg.Fx.Props"),
	TEXT("swg.Fx.Props - counts of the props carrying cdf attachments and what is running: flags, lights, particles."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		USWGPropAttachmentsSubsystem* Props = FindPropAttachments(World);
		if (!Props)
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.Fx.Props: no running game"));
			return;
		}
		Props->RefreshNow();
		const FSWGPropAttachmentStats Stats = Props->GetStats();
		UE_LOG(LogTemp, Display, TEXT("swg.Fx.Props: %d prop(s); %d near enough for flags, %d for effects; flags running %d; lights lit %d (%d wanted one); particles live %d; wind %.2f"),
			Stats.Props, Stats.FlagProps, Stats.FxProps, Stats.FlagsRunning, Stats.LightsLit, Stats.LightCandidates, Stats.ParticlesLive, Props->GetWindStrength());
	}));

static FAutoConsoleCommandWithWorldAndArgs GWindCommand(
	TEXT("swg.Fx.Wind"),
	TEXT("swg.Fx.Wind <0..1> - flag and banner wind strength until the weather subsystem drives it (0.5 is medium)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		USWGPropAttachmentsSubsystem* Props = FindPropAttachments(World);
		if (!Props)
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.Fx.Wind: no running game"));
			return;
		}
		if (Args.IsEmpty())
		{
			UE_LOG(LogTemp, Display, TEXT("swg.Fx.Wind: %.2f"), Props->GetWindStrength());
			return;
		}
		Props->SetWindStrength(FCString::Atof(*Args[0]));
		Props->RefreshNow();
		UE_LOG(LogTemp, Display, TEXT("swg.Fx.Wind: set to %.2f"), Props->GetWindStrength());
	}));

#endif // !UE_BUILD_SHIPPING
