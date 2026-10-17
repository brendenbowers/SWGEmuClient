#include "Subsystems/SWGWorldParticlesSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Common/SWGWorldScale.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "SWGEffectsSubsystem.h"

namespace
{
	TAutoConsoleVariable<float> CVarPlacedRadius(
		TEXT("swg.Fx.WorldRadius"), 100.f,
		TEXT("Metres within which particle objects (campfires, smoke, electricity) play."));

	TAutoConsoleVariable<float> CVarFloraRadius(
		TEXT("swg.Fx.FloraRadius"), 70.f,
		TEXT("Metres within which flora particles (insects, butterflies, fish, lava) play."));

	TAutoConsoleVariable<float> CVarHysteresis(
		TEXT("swg.Fx.WorldHysteresis"), 1.25f,
		TEXT("A running world emitter stops only beyond radius x this, so emitters at the edge do not flicker."));

	TAutoConsoleVariable<int32> CVarMaxEmitters(
		TEXT("swg.Fx.MaxWorldEmitters"), 48,
		TEXT("Most world emitters (objects and flora together) playing at once; the nearest win."));

	TAutoConsoleVariable<float> CVarUpdateHz(
		TEXT("swg.Fx.WorldUpdateHz"), 4.f,
		TEXT("How many times a second world emitter distance gating is re-evaluated."));

	TAutoConsoleVariable<int32> CVarEnabled(
		TEXT("swg.Fx.WorldParticles"), 1,
		TEXT("0 stops all world particle emitters (objects and flora)."));
}

bool USWGWorldParticlesSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void USWGWorldParticlesSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<USWGEffectsSubsystem>();
}

void USWGWorldParticlesSubsystem::Deinitialize()
{
	for (TPair<int32, FEmitter>& Pair : Emitters)
	{
		StopEmitter(Pair.Value);
	}
	Emitters.Reset();
	Super::Deinitialize();
}

TStatId USWGWorldParticlesSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGWorldParticlesSubsystem, STATGROUP_Tickables);
}

int32 USWGWorldParticlesSubsystem::AddEmitter(ESWGWorldEmitterKind Kind, const FString& PrtPath, const FTransform& WorldTransform, AActor* Owner, const FTransform* OwnerRelative)
{
	if (PrtPath.IsEmpty())
	{
		return 0;
	}

	const int32 EmitterId = NextEmitterId++;
	FEmitter& Emitter = Emitters.Add(EmitterId);
	Emitter.Kind = Kind;
	Emitter.PrtPath = PrtPath;
	Emitter.Transform = WorldTransform;
	Emitter.Owner = Owner;
	Emitter.bHasOwner = Owner != nullptr;
	Emitter.OwnerRelative = OwnerRelative ? *OwnerRelative : FTransform::Identity;
	return EmitterId;
}

void USWGWorldParticlesSubsystem::RemoveEmitter(int32 EmitterId)
{
	FEmitter Removed;
	if (Emitters.RemoveAndCopyValue(EmitterId, Removed))
	{
		StopEmitter(Removed);
	}
}

void USWGWorldParticlesSubsystem::StopEmitter(FEmitter& Emitter)
{
	if (Emitter.Handle.IsValid())
	{
		Emitter.Handle.Stop(true);
	}
	Emitter.Handle = FSWGEffectHandle();
	Emitter.bEngaged = false;
}

void USWGWorldParticlesSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	SecondsSinceRefresh += DeltaTime;
	if (SecondsSinceRefresh >= 1.f / FMath::Clamp(CVarUpdateHz.GetValueOnGameThread(), 0.5f, 30.f))
	{
		Refresh();
	}
}

void USWGWorldParticlesSubsystem::RefreshNow()
{
	Refresh();
}

void USWGWorldParticlesSubsystem::Refresh()
{
	SecondsSinceRefresh = 0.f;
	USWGEffectsSubsystem* Effects = GetWorld() ? GetWorld()->GetSubsystem<USWGEffectsSubsystem>() : nullptr;
	const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Effects || !Controller)
	{
		return;
	}
	const FVector ViewLocation = Controller->PlayerCameraManager ? Controller->PlayerCameraManager->GetCameraLocation() : Controller->GetFocalLocation();

	const bool bEnabled = CVarEnabled.GetValueOnGameThread() != 0;
	const float Hysteresis = FMath::Max(1.f, CVarHysteresis.GetValueOnGameThread());
	const float RadiusCm[2] = { SWGToUnrealSpace(CVarPlacedRadius.GetValueOnGameThread()), SWGToUnrealSpace(CVarFloraRadius.GetValueOnGameThread()) };

	struct FCandidate
	{
		double DistanceSquared;
		FEmitter* Emitter;
	};
	TArray<FCandidate> Candidates;

	for (TPair<int32, FEmitter>& Pair : Emitters)
	{
		FEmitter& Emitter = Pair.Value;
		// A server object is placed after it spawns (cell containment), so follow it until the next play.
		if (Emitter.bHasOwner && Emitter.Owner.IsValid())
		{
			Emitter.Transform = Emitter.OwnerRelative * Emitter.Owner->GetActorTransform();
		}
		const double DistanceSquared = FVector::DistSquared(ViewLocation, Emitter.Transform.GetLocation());
		const float Radius = RadiusCm[(int32)Emitter.Kind] * (Emitter.bEngaged ? Hysteresis : 1.f);
		const bool bOwnerMuted = Emitter.bHasOwner && (!Emitter.Owner.IsValid() || Emitter.Owner->IsHidden());

		Emitter.bEngaged = bEnabled && !bOwnerMuted && DistanceSquared < FMath::Square(Radius);
		if (Emitter.bEngaged)
		{
			Candidates.Add({ DistanceSquared, &Emitter });
		}
		else if (Emitter.Handle.IsValid())
		{
			StopEmitter(Emitter);
		}
	}

	Candidates.Sort([](const FCandidate& Left, const FCandidate& Right) { return Left.DistanceSquared < Right.DistanceSquared; });

	const int32 MaxEmitters = FMath::Max(0, CVarMaxEmitters.GetValueOnGameThread());
	for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
	{
		FEmitter& Emitter = *Candidates[CandidateIndex].Emitter;
		if (CandidateIndex >= MaxEmitters)
		{
			StopEmitter(Emitter);
		}
		else if (!Emitter.Handle.IsActive())
		{
			// Also restarts a one-shot that finished, so a puff keeps puffing.
			Emitter.Handle = Effects->PlayParticleAtLocation(Emitter.PrtPath, Emitter.Transform);
		}
	}
}

FSWGWorldParticleStats USWGWorldParticlesSubsystem::GetStats() const
{
	FSWGWorldParticleStats Stats;
	for (const TPair<int32, FEmitter>& Pair : Emitters)
	{
		const FEmitter& Emitter = Pair.Value;
		const bool bPlaced = Emitter.Kind == ESWGWorldEmitterKind::Placed;
		(bPlaced ? Stats.PlacedEmitters : Stats.FloraEmitters) += 1;
		(bPlaced ? Stats.PlacedRunning : Stats.FloraRunning) += Emitter.Handle.IsActive() ? 1 : 0;
	}
	if (const USWGEffectsSubsystem* Effects = GetWorld() ? GetWorld()->GetSubsystem<USWGEffectsSubsystem>() : nullptr)
	{
		const FSWGEffectsStats EffectStats = Effects->GetStats();
		Stats.LiveEffects = EffectStats.ActiveEffects;
		Stats.SkeletalParticles = EffectStats.SkeletalParticles;
	}
	return Stats;
}
