#include "Objects/World/SWGSoundEmitter.h"

#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "SWGAudioSubsystem.h"
#include "Objects/World/SWGParticleEmitterActor.h"
#include "Subsystems/SWGAmbienceSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGWorldParticlesSubsystem.h"

namespace
{
	// Stop an out-of-range loop only after it has drifted this far past its radius, so it doesn't flap at the edge.
	constexpr float LoopStopMargin = 1.25f;
}

ASWGSoundEmitter::ASWGSoundEmitter()
{
	PrimaryActorTick.bCanEverTick = false;
}

bool ASWGSoundEmitter::IsSoundObjectTemplate(const FString& TemplatePath)
{
	return TemplatePath.StartsWith(TEXT("object/soundobject/"), ESearchCase::IgnoreCase);
}

USWGAudioSubsystem* ASWGSoundEmitter::GetAudio() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<USWGAudioSubsystem>() : nullptr;
}

void ASWGSoundEmitter::InitFromTemplate(const FString& InTemplatePath)
{
	TemplatePath = InTemplatePath;
	StaticTemplatePath = InTemplatePath;

	UGameInstance* GameInstance = GetGameInstance();

	// Many sound objects carry a .prt appearance (sparks, steam, fire) that plays where the sound does.
	USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	USWGWorldParticlesSubsystem* Particles = GetWorld() ? GetWorld()->GetSubsystem<USWGWorldParticlesSubsystem>() : nullptr;
	FString ParticlePath;
	if (Tre && Particles && ASWGParticleEmitterActor::ResolveParticlePath(*Tre, TemplatePath, ParticlePath))
	{
		Particles->RemoveEmitter(ParticleEmitterId);
		ParticleEmitterId = Particles->AddEmitter(ESWGWorldEmitterKind::Placed, ParticlePath, GetActorTransform(), this);
	}
	USWGAmbienceSubsystem* Ambience = GameInstance ? GameInstance->GetSubsystem<USWGAmbienceSubsystem>() : nullptr;
	USWGAudioSubsystem* Audio = GetAudio();
	if (!Ambience || !Audio)
	{
		return;
	}

	Loops.Reset();
	MaxRadius = 0.f;
	for (const FString& Path : Ambience->ResolveSoundObjectLoops(TemplatePath))
	{
		FLoop& Loop = Loops.AddDefaulted_GetRef();
		Loop.Path = Path;
		// A 2D-kind .snd is spatialised anyway; its t27 distance is a range.
		Loop.Radius = Audio->GetAudibleRadius(Path, /*bForceSpatial*/ true);
		MaxRadius = FMath::Max(MaxRadius, Loop.Radius);
	}

	if (!Loops.IsEmpty())
	{
		Ambience->RegisterEmitter(this);
	}
}

void ASWGSoundEmitter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopLoops();
	if (USWGWorldParticlesSubsystem* Particles = GetWorld() ? GetWorld()->GetSubsystem<USWGWorldParticlesSubsystem>() : nullptr)
	{
		Particles->RemoveEmitter(ParticleEmitterId);
	}
	ParticleEmitterId = 0;
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (USWGAmbienceSubsystem* Ambience = GameInstance->GetSubsystem<USWGAmbienceSubsystem>())
		{
			Ambience->UnregisterEmitter(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ASWGSoundEmitter::StopLoops()
{
	for (const FLoop& Loop : Loops)
	{
		Loop.Handle.Stop(0.f);
	}
	for (FLoop& Loop : Loops)
	{
		Loop.bActive = false;
	}
}

void ASWGSoundEmitter::UpdateLoops(const FVector& ListenerLocation, bool bAllowed)
{
	USWGAudioSubsystem* Audio = GetAudio();
	USceneComponent* Root = GetRootComponent();
	if (!Audio || !Root)
	{
		return;
	}

	const float Distance = static_cast<float>(FVector::Dist(ListenerLocation, GetActorLocation()));
	for (FLoop& Loop : Loops)
	{
		const bool bInRange = bAllowed && Loop.Radius > 0.f && Distance <= Loop.Radius;
		if (!Loop.bActive && bInRange)
		{
			FSWGSoundPlayOptions Options;
			Options.bForceSpatial = true;
			Loop.Handle = Audio->PlaySoundAttached(Loop.Path, Root, NAME_None, Options);
			Loop.bActive = true;
		}
		else if (Loop.bActive && (!bAllowed || Distance > Loop.Radius * LoopStopMargin))
		{
			Loop.Handle.Stop(0.5f);
			Loop.bActive = false;
		}
	}
}

int32 ASWGSoundEmitter::GetActiveLoopCount() const
{
	int32 Count = 0;
	for (const FLoop& Loop : Loops)
	{
		Count += Loop.bActive && Loop.Handle.IsPlaying() ? 1 : 0;
	}
	return Count;
}

TArray<FString> ASWGSoundEmitter::GetLoopDescriptions() const
{
	TArray<FString> Result;
	for (const FLoop& Loop : Loops)
	{
		Result.Add(FString::Printf(TEXT("%s radius %.0f cm %s"), *Loop.Path, Loop.Radius, Loop.bActive ? TEXT("PLAYING") : TEXT("idle")));
	}
	return Result;
}
