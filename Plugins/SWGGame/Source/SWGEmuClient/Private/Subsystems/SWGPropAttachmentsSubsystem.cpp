#include "Subsystems/SWGPropAttachmentsSubsystem.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Common/SWGWorldScale.h"
#include "Components/SWGPropAttachmentsComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Objects/SWGObject.h"
#include "Objects/World/SWGSoundEmitter.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	TAutoConsoleVariable<float> CVarFlagRadius(
		TEXT("swg.Fx.PropFlagRadius"), 200.f,
		TEXT("Metres within which flags and banners animate."));

	TAutoConsoleVariable<float> CVarFxRadius(
		TEXT("swg.Fx.PropFxRadius"), 120.f,
		TEXT("Metres within which prop particles (waterfalls, glows, torches) and lights run."));

	TAutoConsoleVariable<float> CVarHysteresis(
		TEXT("swg.Fx.PropHysteresis"), 1.25f,
		TEXT("A running prop effect stops only beyond radius x this, so props at the edge do not flicker on and off."));

	TAutoConsoleVariable<int32> CVarMaxLights(
		TEXT("swg.Fx.MaxPropLights"), 16,
		TEXT("Most prop point lights lit at once; the nearest win."));

	TAutoConsoleVariable<float> CVarUpdateHz(
		TEXT("swg.Fx.PropUpdateHz"), 4.f,
		TEXT("How many times a second prop distance gating is re-evaluated."));

	TAutoConsoleVariable<float> CVarLightIntensity(
		TEXT("swg.Fx.PropLightIntensity"), 125.f,
		TEXT("Candelas of a prop light whose cdf brightness is 1 (a streetlamp)."));

	TAutoConsoleVariable<int32> CVarEnabled(
		TEXT("swg.Fx.PropAttachments"), 1,
		TEXT("0 stops all prop flags, particles and lights (children stay placed)."));

	// A cdf names these as objects or appearances; only the particle kind plays here.
	bool IsParticlePath(const FString& Path)
	{
		return Path.EndsWith(TEXT(".prt"), ESearchCase::IgnoreCase);
	}

	bool HoldsAttachments(const FSWGClientDataFile& Data)
	{
		if (!Data.Flags.IsEmpty() || !Data.Lights.IsEmpty() || !Data.Children.IsEmpty())
		{
			return true;
		}
		return Data.HardpointObjects.ContainsByPredicate([](const FSWGClientDataHardpointObject& Object) { return IsParticlePath(Object.AppearancePath); });
	}
}

bool USWGPropAttachmentsSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void USWGPropAttachmentsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		MeshGenerator = GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>();
		Terrain = GameInstance->GetSubsystem<USWGTerrainSubsystem>();
	}
	if (MeshGenerator)
	{
		MeshReadyHandle = MeshGenerator->OnMeshReady.AddUObject(this, &USWGPropAttachmentsSubsystem::HandleMeshReady);
	}
}

void USWGPropAttachmentsSubsystem::Deinitialize()
{
	if (MeshGenerator)
	{
		MeshGenerator->OnMeshReady.Remove(MeshReadyHandle);
	}
	Props.Reset();
	LitProps.Reset();
	AttachmentsByTemplate.Reset();
	MeshGenerator = nullptr;
	Terrain = nullptr;
	Super::Deinitialize();
}

TStatId USWGPropAttachmentsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGPropAttachmentsSubsystem, STATGROUP_Tickables);
}

TSharedPtr<const FSWGClientDataFile> USWGPropAttachmentsSubsystem::FindAttachments(const FString& TemplatePath)
{
	if (TemplatePath.IsEmpty() || !MeshGenerator)
	{
		return nullptr;
	}
	if (const TSharedPtr<const FSWGClientDataFile>* Cached = AttachmentsByTemplate.Find(TemplatePath))
	{
		return *Cached;
	}

	// Cached either way: most templates have no cdf, and the lookup walks the DERV chain.
	TSharedPtr<const FSWGClientDataFile> Result;
	FSWGClientDataFile Data;
	if (MeshGenerator->ReadClientDataFile(TemplatePath, Data) && HoldsAttachments(Data))
	{
		Result = MakeShared<const FSWGClientDataFile>(MoveTemp(Data));
	}
	AttachmentsByTemplate.Add(TemplatePath, Result);
	return Result;
}

void USWGPropAttachmentsSubsystem::HandleMeshReady(AActor* Actor, UMeshComponent* MeshComponent)
{
	if (!Actor || Actor->GetWorld() != GetWorld())
	{
		return;
	}
	if (!IsInGameThread())
	{
		TWeakObjectPtr<AActor> ActorWeak(Actor);
		TWeakObjectPtr<USWGPropAttachmentsSubsystem> ThisWeak(this);
		AsyncTask(ENamedThreads::GameThread, [ActorWeak, ThisWeak]()
			{
				if (AActor* GameActor = ActorWeak.Get(); GameActor && ThisWeak.IsValid())
				{
					ThisWeak->AttachToActor(*GameActor);
				}
			});
		return;
	}
	AttachToActor(*Actor);
}

void USWGPropAttachmentsSubsystem::AttachToActor(AActor& Actor)
{
	ASWGObject* Object = Cast<ASWGObject>(&Actor);
	if (!Object || Cast<ASWGSoundEmitter>(Object) || Actor.FindComponentByClass<USWGPropAttachmentsComponent>() || !GetWorld())
	{
		return;
	}

	FString TemplatePath = Object->StaticTemplatePath;
	if (TemplatePath.IsEmpty() && Object->SWGObjectCRC != 0)
	{
		const USWGTreSubsystem* Tre = GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<USWGTreSubsystem>() : nullptr;
		TemplatePath = Tre ? Tre->ResolveTemplatePath(Object->SWGObjectCRC) : FString();
	}

	const TSharedPtr<const FSWGClientDataFile> Data = FindAttachments(TemplatePath);
	if (!Data.IsValid())
	{
		return;
	}

	USWGPropAttachmentsComponent* Component = NewObject<USWGPropAttachmentsComponent>(&Actor, NAME_None, RF_Transient);
	Component->Setup(*this, Data.ToSharedRef(), !Actor.ActorHasTag(TEXT("SWGPropChild")));
	Component->RegisterComponent();
}

void USWGPropAttachmentsSubsystem::RegisterProp(USWGPropAttachmentsComponent* Prop)
{
	Props.AddUnique(Prop);
}

void USWGPropAttachmentsSubsystem::UnregisterProp(USWGPropAttachmentsComponent* Prop)
{
	Props.Remove(Prop);
	LitProps.Remove(Prop);
}

float USWGPropAttachmentsSubsystem::GetLightIntensityScale() const
{
	return CVarLightIntensity.GetValueOnGameThread();
}

void USWGPropAttachmentsSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const float Hz = FMath::Clamp(CVarUpdateHz.GetValueOnGameThread(), 0.5f, 30.f);
	SecondsSinceRefresh += DeltaTime;
	if (SecondsSinceRefresh >= 1.f / Hz)
	{
		SecondsSinceRefresh = 0.f;
		Refresh();
	}

	// The lit set is at most swg.Fx.MaxPropLights, so a per-frame flicker is cheap.
	const float TimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	for (const TWeakObjectPtr<USWGPropAttachmentsComponent>& Prop : LitProps)
	{
		if (USWGPropAttachmentsComponent* Live = Prop.Get())
		{
			Live->TickFlicker(TimeSeconds);
		}
	}
}

void USWGPropAttachmentsSubsystem::RefreshNow()
{
	Refresh();
	SecondsSinceRefresh = 0.f;
}

void USWGPropAttachmentsSubsystem::Refresh()
{
	Props.RemoveAll([](const TWeakObjectPtr<USWGPropAttachmentsComponent>& Prop) { return !Prop.IsValid(); });

	const bool bEnabled = CVarEnabled.GetValueOnGameThread() != 0;
	FVector ViewLocation = FVector::ZeroVector;
	const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (Controller)
	{
		ViewLocation = Controller->PlayerCameraManager ? Controller->PlayerCameraManager->GetCameraLocation() : Controller->GetFocalLocation();
	}

	// Interior props ignore this: a room has no sun to wait for.
	bNight = Terrain && !Terrain->IsDaytime();

	const float Hysteresis = FMath::Max(1.f, CVarHysteresis.GetValueOnGameThread());
	const float FlagRadiusCm = SWGToUnrealSpace(CVarFlagRadius.GetValueOnGameThread());
	const float FxRadiusCm = SWGToUnrealSpace(CVarFxRadius.GetValueOnGameThread());

	struct FLightCandidate
	{
		float DistanceSquared;
		USWGPropAttachmentsComponent* Prop;
	};
	TArray<FLightCandidate> Candidates;

	for (const TWeakObjectPtr<USWGPropAttachmentsComponent>& PropWeak : Props)
	{
		USWGPropAttachmentsComponent* Prop = PropWeak.Get();
		if (!Prop)
		{
			continue;
		}

		const float DistanceSquared = Controller ? FVector::DistSquared(ViewLocation, Prop->GetOwner()->GetActorLocation()) : TNumericLimits<float>::Max();
		const float FlagLimit = Prop->IsFlagsActive() ? FlagRadiusCm * Hysteresis : FlagRadiusCm;
		const float FxLimit = Prop->IsEffectsActive() ? FxRadiusCm * Hysteresis : FxRadiusCm;

		Prop->SetEnvironment(bNight, WindStrength);
		Prop->SetFlagsActive(bEnabled && DistanceSquared < FMath::Square(FlagLimit));
		Prop->SetEffectsActive(bEnabled && DistanceSquared < FMath::Square(FxLimit));
		if (Prop->WantsLight())
		{
			Candidates.Add({ DistanceSquared, Prop });
		}
		else
		{
			Prop->SetLightsAllowed(false);
		}
	}

	Candidates.Sort([](const FLightCandidate& Left, const FLightCandidate& Right) { return Left.DistanceSquared < Right.DistanceSquared; });

	const int32 MaxLights = FMath::Max(0, CVarMaxLights.GetValueOnGameThread());
	LitProps.Reset();
	for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
	{
		const bool bAllowed = CandidateIndex < MaxLights;
		Candidates[CandidateIndex].Prop->SetLightsAllowed(bAllowed);
		if (bAllowed)
		{
			LitProps.Add(Candidates[CandidateIndex].Prop);
		}
	}
	LastStats.LightCandidates = Candidates.Num();
}

FSWGPropAttachmentStats USWGPropAttachmentsSubsystem::GetStats() const
{
	FSWGPropAttachmentStats Stats;
	Stats.LightCandidates = LastStats.LightCandidates;
	for (const TWeakObjectPtr<USWGPropAttachmentsComponent>& PropWeak : Props)
	{
		if (const USWGPropAttachmentsComponent* Prop = PropWeak.Get())
		{
			++Stats.Props;
			Stats.FlagProps += Prop->IsFlagsActive() ? 1 : 0;
			Stats.FxProps += Prop->IsEffectsActive() ? 1 : 0;
			Stats.FlagsRunning += Prop->CountPlayingFlags();
			Stats.LightsLit += Prop->CountLitLights();
			Stats.ParticlesLive += Prop->CountLiveParticles();
		}
	}
	return Stats;
}
