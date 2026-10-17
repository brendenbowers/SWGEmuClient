#include "Subsystems/SWGWorldParticlesSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace
{
	// From the editor's console the world is the editor's; use the running game's instead.
	USWGWorldParticlesSubsystem* FindWorldParticles(UWorld* World)
	{
		if (World && World->IsGameWorld())
		{
			return World->GetSubsystem<USWGWorldParticlesSubsystem>();
		}
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
				{
					return Context.World()->GetSubsystem<USWGWorldParticlesSubsystem>();
				}
			}
		}
		return nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GWorldFxCommand(
	TEXT("swg.Fx.World"),
	TEXT("swg.Fx.World - counts of the world particle emitters (snapshot/server objects and flora), how many play, and skeletal mesh particles."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		USWGWorldParticlesSubsystem* Particles = FindWorldParticles(World);
		if (!Particles)
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.Fx.World: no running game"));
			return;
		}
		Particles->RefreshNow();
		const FSWGWorldParticleStats Stats = Particles->GetStats();
		UE_LOG(LogTemp, Display, TEXT("swg.Fx.World: %d object emitter(s) (%d playing), %d flora emitter(s) (%d playing); %d effect(s) active in total; %d skeletal mesh particle(s)"),
			Stats.PlacedEmitters, Stats.PlacedRunning, Stats.FloraEmitters, Stats.FloraRunning, Stats.LiveEffects, Stats.SkeletalParticles);
	}));

#endif // !UE_BUILD_SHIPPING
