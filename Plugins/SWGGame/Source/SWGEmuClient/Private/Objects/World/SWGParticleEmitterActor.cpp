#include "Objects/World/SWGParticleEmitterActor.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGWorldParticlesSubsystem.h"
#include "TRE/SWGIffReader.h"

ASWGParticleEmitterActor::ASWGParticleEmitterActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

bool ASWGParticleEmitterActor::IsParticleObjectTemplate(const FString& TemplatePath)
{
	return TemplatePath.StartsWith(TEXT("object/static/particle/"), ESearchCase::IgnoreCase);
}

bool ASWGParticleEmitterActor::ResolveParticlePath(USWGTreSubsystem& Tre, const FString& TemplatePath, FString& OutPrtPath)
{
	// Snapshot resolution asks for every STAT node on worker threads; an empty entry remembers "not a particle".
	static FCriticalSection CacheLock;
	static TMap<FString, FString> CachedPaths;
	{
		FScopeLock Lock(&CacheLock);
		if (const FString* Cached = CachedPaths.Find(TemplatePath))
		{
			OutPrtPath = *Cached;
			return !Cached->IsEmpty();
		}
	}

	FString Appearance;
	if (!Tre.FindTemplateFormStringParam(TemplatePath, SWG_IFF_TAG('S', 'H', 'O', 'T'), TEXT("appearanceFilename"), Appearance)
		|| !Appearance.EndsWith(TEXT(".prt"), ESearchCase::IgnoreCase))
	{
		Appearance.Reset();
	}
	{
		FScopeLock Lock(&CacheLock);
		CachedPaths.Add(TemplatePath, Appearance);
	}
	OutPrtPath = Appearance;
	return !Appearance.IsEmpty();
}

void ASWGParticleEmitterActor::InitFromTemplate(const FString& InTemplatePath)
{
	StaticTemplatePath = InTemplatePath;

	UGameInstance* GameInstance = GetGameInstance();
	USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	USWGWorldParticlesSubsystem* Particles = GetWorld() ? GetWorld()->GetSubsystem<USWGWorldParticlesSubsystem>() : nullptr;
	if (!Tre || !Particles || !ResolveParticlePath(*Tre, InTemplatePath, ParticlePath))
	{
		return;
	}

	Particles->RemoveEmitter(EmitterId);
	EmitterId = Particles->AddEmitter(ESWGWorldEmitterKind::Placed, ParticlePath, GetActorTransform(), this);
}

void ASWGParticleEmitterActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (USWGWorldParticlesSubsystem* Particles = GetWorld() ? GetWorld()->GetSubsystem<USWGWorldParticlesSubsystem>() : nullptr)
	{
		Particles->RemoveEmitter(EmitterId);
	}
	EmitterId = 0;
	Super::EndPlay(EndPlayReason);
}
