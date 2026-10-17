#include "Subsystems/SWGEffectSubsystem.h"
#include "SWGLogCategories.h"
#include "Common/SWGRetailLight.h"
#include "Common/SWGWorldScale.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/MeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Shakes/LegacyCameraShake.h"
#include "SWGEffectsSubsystem.h"
#include "TimerManager.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/EffectMessages.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"
#include "SWGAudioSubsystem.h"
#include "SWGAudioTypes.h"
#include "SWGMusicManager.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "TRE/SWGClientEffectReader.h"

DEFINE_LOG_CATEGORY(LogSWGEffect);

namespace
{
	TAutoConsoleVariable<float> CVarEffectLightIntensity(
		TEXT("swg.Effect.LightIntensity"), 5000.f,
		TEXT("Candela of a .cef CLGT point light at full strength; it holds full strength for its lifetime (retail does not fade)."));
	TAutoConsoleVariable<float> CVarEffectParticleSeconds(
		TEXT("swg.Effect.ParticleMaxSeconds"), 5.f,
		TEXT("Seconds before a .cef particle is faded out. Looping particles never end on their own."));
	TAutoConsoleVariable<bool> CVarEffectShake(
		TEXT("swg.Effect.Shake"), true,
		TEXT("Plays a .cef CAMS camera shake when the local camera is within its range."));
	TAutoConsoleVariable<float> CVarEffectShakeScale(
		TEXT("swg.Effect.ShakeScale"), 1.f,
		TEXT("Multiplier on the CAMS shake magnitude before distance falloff."));

	/** Light fade step; 30 Hz is smooth for a muzzle flash or a grenade flare. */
	constexpr float LightFadeStepSeconds = 1.f / 30.f;
	/** Shortest light duration: a zero CLGT duration would not show at all. */
	constexpr float MinLightSeconds = 0.05f;
	constexpr float MinShakeSeconds = 0.05f;
	// Retail rolls a fresh random rotation every frame; a fast oscillation approximates that noise.
	constexpr float ShakeNoiseHertz = 25.f;

	/** State of one fading light, shared by its timer callback. */
	struct FLightFade
	{
		TWeakObjectPtr<UPointLightComponent> Light;
		float BaseIntensity = 0.f;
		float DurationSeconds = 0.f;
		float ElapsedSeconds = 0.f;
		FTimerHandle Timer;
	};

	int32 CountStarted(const TArray<FSWGSoundHandle>& Handles)
	{
		int32 Started = 0;
		for (const FSWGSoundHandle& Handle : Handles)
		{
			Started += Handle.IsValid() ? 1 : 0;
		}
		return Started;
	}

	/** First mesh on the actor that has the socket; FName compares case-insensitively. */
	UMeshComponent* FindSocketMesh(const AActor* Actor, FName Socket)
	{
		TInlineComponentArray<UMeshComponent*> Meshes(Actor);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (Mesh->DoesSocketExist(Socket))
			{
				return Mesh;
			}
		}
		return nullptr;
	}
}

void USWGEffectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Network = Cast<USWGNetworkSubsystem>(Collection.InitializeDependency(USWGNetworkSubsystem::StaticClass()));
	ObjectGraph = Cast<USWGObjectGraphSubsystem>(Collection.InitializeDependency(USWGObjectGraphSubsystem::StaticClass()));
	Audio = Cast<USWGAudioSubsystem>(Collection.InitializeDependency(USWGAudioSubsystem::StaticClass()));
	Terrain = Cast<USWGTerrainSubsystem>(Collection.InitializeDependency(USWGTerrainSubsystem::StaticClass()));

	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGEffectSubsystem::HandleMessageReceived);
	}
}

void USWGEffectSubsystem::Deinitialize()
{
	if (Network && MessageHandle.IsValid())
	{
		Network->OnMessageReceived.Remove(MessageHandle);
		MessageHandle.Reset();
	}
	Super::Deinitialize();
}

void USWGEffectSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message)
{
	if (!Message)
	{
		return;
	}

	switch (static_cast<ESWGMessageOp>(Message->Opcode))
	{
		case ESWGMessageOp::PlayMusicMessage:
		{
			const FPlayMusicMessage& Music = *static_cast<const FPlayMusicMessage*>(Message.Get());
			UE_LOG(LogSWGEffect, Log, TEXT("PlayMusic '%s'"), *Music.SoundFile);
			if (USWGMusicManager* Manager = Audio ? Audio->GetMusicManager() : nullptr)
			{
				Manager->SetLayer(ESWGMusicLayer::Server, Music.SoundFile);
			}
			break;
		}
		case ESWGMessageOp::PlayClientEffectObjectMessage:
		{
			const FPlayClientEffectObjectMessage& Effect = *static_cast<const FPlayClientEffectObjectMessage*>(Message.Get());
			PlayEffectOnObject(Effect.EffectFile, static_cast<int64>(Effect.ObjectId), Effect.Hardpoint);
			break;
		}
		case ESWGMessageOp::PlayClientEffectLoc:
		{
			const FPlayClientEffectLocMessage& Effect = *static_cast<const FPlayClientEffectLocMessage*>(Message.Get());
			// Wire order is x, z (up), y (north); raw space is x east, y north, z up.
			PlayEffectAtLocation(Effect.EffectFile, Effect.Planet, FVector(Effect.X, Effect.Y, Effect.Z), static_cast<int64>(Effect.CellId));
			break;
		}
		case ESWGMessageOp::PlayClientEffectObjectTransformMessage:
		{
			const auto& Effect = *static_cast<const FPlayClientEffectObjectTransformMessage*>(Message.Get());
			if (!Effect.bValid) break;
			FQuat Rotation = SWGNativeToUnrealRotation(Effect.RotationX, Effect.RotationY, Effect.RotationZ, Effect.RotationW);
			Rotation.Normalize();
			PlayEffectOnObjectTransform(Effect.EffectFile, int64(Effect.ObjectId), FTransform(Rotation, SWGToUnrealSpace(FVector(Effect.X, Effect.Z, Effect.Y))));
			break;
		}
		case ESWGMessageOp::PlayClientEventObjectMessage:
		{
			const FPlayClientEventObjectMessage& Event = *static_cast<const FPlayClientEventObjectMessage*>(Message.Get());
			UE_LOG(LogSWGEffect, Log, TEXT("PlayClientEvent '%s' aux '%s' on %llu (not implemented)"), *Event.EventName, *Event.Aux, Event.ObjectId);
			break;
		}
		default:
			break;
	}
}

int32 USWGEffectSubsystem::PlayEffectOnObject(const FString& EffectFile, int64 ObjectId, const FString& Hardpoint)
{
	if (!Audio || !ObjectGraph)
	{
		return 0;
	}

	AActor* Actor = ObjectGraph->FindActor(ObjectId);
	USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr;
	if (!Root)
	{
		// No last-known position is kept for objects without an actor, so there is nothing to fall back to.
		UE_LOG(LogSWGEffect, Verbose, TEXT("effect '%s': object %lld is not spawned, skipped"), *EffectFile, ObjectId);
		return 0;
	}

	USceneComponent* Parent = Root;
	FName Socket = NAME_None;
	if (!Hardpoint.IsEmpty())
	{
		if (UMeshComponent* Mesh = FindSocketMesh(Actor, FName(*Hardpoint)))
		{
			Parent = Mesh;
			Socket = FName(*Hardpoint);
		}
		else
		{
			UE_LOG(LogSWGEffect, Log, TEXT("effect '%s': object %lld has no socket '%s', using its root"), *EffectFile, ObjectId, *Hardpoint);
		}
	}

	const int32 Started = PlayClientEffectAttached(EffectFile, Parent, Socket);
	UE_LOG(LogSWGEffect, Log, TEXT("effect '%s' on %lld (%s): %d sound(s)"), *EffectFile, ObjectId, *Hardpoint, Started);
	return Started;
}

int32 USWGEffectSubsystem::PlayEffectOnObjectTransform(const FString& EffectFile, int64 ObjectId, const FTransform& LocalTransform)
{
	AActor* Actor = ObjectGraph ? ObjectGraph->FindActor(ObjectId) : nullptr;
	USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr;
	return Root ? PlayClientEffectAttached(EffectFile, Root, NAME_None, LocalTransform) : 0;
}

int32 USWGEffectSubsystem::PlayEffectAtLocation(const FString& EffectFile, const FString& Planet, const FVector& RawPosition, int64 CellId)
{
	if (!Audio || !ObjectGraph)
	{
		return 0;
	}

	const FString CurrentPlanet = Terrain ? Terrain->GetActivePlanetName() : FString();
	if (!Planet.IsEmpty() && !CurrentPlanet.IsEmpty() && !Planet.Equals(CurrentPlanet, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogSWGEffect, Log, TEXT("effect '%s': planet '%s' is not the current '%s', ignored"), *EffectFile, *Planet, *CurrentPlanet);
		return 0;
	}

	FVector WorldPosition = SWGToUnrealSpace(RawPosition);
	if (CellId != 0)
	{
		// Cell-local is building-relative, the same composition as UpdateTransformMessageWithParent.
		const ASWGCell* Cell = Cast<ASWGCell>(ObjectGraph->FindActor(CellId));
		const ASWGBuilding* Building = Cell ? Cell->OwningBuilding.Get() : nullptr;
		if (!Building)
		{
			UE_LOG(LogSWGEffect, Verbose, TEXT("effect '%s': cell %lld is not built, skipped"), *EffectFile, CellId);
			return 0;
		}
		WorldPosition = Building->GetActorTransform().TransformPosition(WorldPosition);
	}

	const int32 Started = PlayClientEffectAtLocation(EffectFile, WorldPosition);
	UE_LOG(LogSWGEffect, Log, TEXT("effect '%s' at %s (cell %lld): %d sound(s)"), *EffectFile, *WorldPosition.ToString(), CellId, Started);
	return Started;
}

USWGEffectSubsystem* USWGEffectSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<USWGEffectSubsystem>() : nullptr;
}

int32 USWGEffectSubsystem::PlayClientEffectAttached(const FString& EffectFile, USceneComponent* Parent, FName Socket, const FTransform& LocalTransform)
{
	if (!Audio || !Parent)
	{
		return 0;
	}
	const TSharedPtr<const FSWGClientEffect> Effect = Audio->GetClientEffect(SWGAudio::NormalizeEffectPath(EffectFile));
	if (!Effect)
	{
		return 0;
	}

	const int32 Started = CountStarted(Audio->PlayClientEffectSoundsAttached(EffectFile, Parent, Socket));
	PlayVisuals(*Effect, Parent, Socket, LocalTransform * Parent->GetSocketTransform(Socket), LocalTransform);
	return Started;
}

int32 USWGEffectSubsystem::PlayClientEffectAtLocation(const FString& EffectFile, const FVector& WorldLocation, const FRotator& Rotation)
{
	if (!Audio)
	{
		return 0;
	}
	const TSharedPtr<const FSWGClientEffect> Effect = Audio->GetClientEffect(SWGAudio::NormalizeEffectPath(EffectFile));
	if (!Effect)
	{
		return 0;
	}

	const int32 Started = CountStarted(Audio->PlayClientEffectSoundsAtLocation(EffectFile, WorldLocation));
	PlayVisuals(*Effect, nullptr, NAME_None, FTransform(Rotation, WorldLocation));
	return Started;
}

void USWGEffectSubsystem::PlayVisuals(const FSWGClientEffect& Effect, USceneComponent* Parent, FName Socket, const FTransform& Where, const FTransform& LocalTransform)
{
	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	if (USWGEffectsSubsystem* Particles = World->GetSubsystem<USWGEffectsSubsystem>())
	{
		for (const FSWGClientEffectParticle& Particle : Effect.Particles)
		{
			const FLinearColor Tint(Particle.Tint[0], Particle.Tint[1], Particle.Tint[2], Particle.Tint[3]);
			const FSWGEffectHandle Handle = Parent
				? Particles->PlayParticleAttached(Particle.Path, Parent, Socket, Particle.Scale, Tint, LocalTransform)
				: Particles->PlayParticleAtLocation(Particle.Path, Where, Particle.Scale, Tint);
			// Stops a looping particle after a while; one-shots have already ended and ignore it.
			FTimerHandle StopTimer;
			World->GetTimerManager().SetTimer(StopTimer, FTimerDelegate::CreateLambda([Handle]() { Handle.Stop(false); }),
				FMath::Max(CVarEffectParticleSeconds.GetValueOnGameThread(), 0.f), false);
		}
	}

	for (const FSWGClientEffectLight& Light : Effect.Lights)
	{
		PlayLight(Light, Parent, Socket, Where, LocalTransform);
	}

	const FVector Origin = Parent ? Parent->GetSocketLocation(Socket) : Where.GetLocation();
	for (const FSWGClientEffectCameraShake& Shake : Effect.CameraShakes)
	{
		PlayCameraShake(Shake, Origin);
	}
}

void USWGEffectSubsystem::PlayLight(const FSWGClientEffectLight& Light, USceneComponent* Parent, FName Socket, const FTransform& Where, const FTransform& LocalTransform)
{
	// SWGEmu.exe 0x5f05b0: Params[0] = lifetime (s, no fade), [1..3] = constant/linear/quadratic attenuation (fitted by SWGRetailLight), [4] = range (m).
	const float RangeMetres = Light.Params[4];
	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World || RangeMetres <= 0.f)
	{
		return;
	}

	const float DurationSeconds = FMath::Max(Light.Params[0], MinLightSeconds);
	if (!Parent || !Parent->GetOwner())
	{
		// Nothing to attach to: a short-lived host actor carries the light.
		AActor* Host = World->SpawnActor<AActor>(AActor::StaticClass(), Where);
		if (!Host)
		{
			return;
		}
		Parent = NewObject<USceneComponent>(Host);
		Host->SetRootComponent(Parent);
		Parent->RegisterComponent();
		Socket = NAME_None;
		Host->SetLifeSpan(DurationSeconds + 2.f * LightFadeStepSeconds);
	}

	UPointLightComponent* Component = NewObject<UPointLightComponent>(Parent->GetOwner());
	Component->SetLightColor(FLinearColor(FColor(Light.Color[0], Light.Color[1], Light.Color[2])));
	Component->SetAttenuationRadius(SWGToUnrealSpace(RangeMetres));
	Component->SetCastShadows(false);
	const float RetailExponent = SWGRetailLight::FitFalloffExponent(Light.Params[1], Light.Params[2], Light.Params[3], RangeMetres);
	const float LightIntensity = SWGRetailLight::ComputeIntensity(RetailExponent, CVarEffectLightIntensity.GetValueOnGameThread(), SWGToUnrealSpace(RangeMetres));
	SWGRetailLight::ApplyFalloff(*Component, RetailExponent);
	Component->SetIntensity(LightIntensity);
	Component->RegisterComponent();
	Component->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform, Socket);
	Component->SetRelativeLocationAndRotation(LocalTransform.GetLocation(), LocalTransform.GetRotation());

	TSharedPtr<FLightFade> Fade = MakeShared<FLightFade>();
	Fade->Light = Component;
	Fade->BaseIntensity = LightIntensity;
	Fade->DurationSeconds = DurationSeconds;
	TWeakObjectPtr<UWorld> WeakWorld(World);
	World->GetTimerManager().SetTimer(Fade->Timer, FTimerDelegate::CreateLambda([Fade, WeakWorld]()
	{
		Fade->ElapsedSeconds += LightFadeStepSeconds;
		UPointLightComponent* Live = Fade->Light.Get();
		if (!Live || Fade->ElapsedSeconds >= Fade->DurationSeconds)
		{
			if (UWorld* LiveWorld = WeakWorld.Get())
			{
				LiveWorld->GetTimerManager().ClearTimer(Fade->Timer);
			}
			if (Live)
			{
				Live->DestroyComponent();
			}
			return;
		}
	}), LightFadeStepSeconds, true);
}

void USWGEffectSubsystem::PlayCameraShake(const FSWGClientEffectCameraShake& Shake, const FVector& Origin)
{
	UGameInstance* GameInstance = GetGameInstance();
	APlayerController* Controller = GameInstance ? GameInstance->GetFirstLocalPlayerController() : nullptr;
	APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager : nullptr;
	if (!CVarEffectShake.GetValueOnGameThread() || !Camera || Shake.Range <= 0.f)
	{
		return;
	}

	const float DistanceMetres = SWGToRawSpace(FVector::Dist(Camera->GetCameraLocation(), Origin));
	if (DistanceMetres >= Shake.Range)
	{
		return;
	}

	// SWGEmu.exe 0x5f0350 / 0x788740: Amplitude is a total angle in radians (rolled in +-half each frame), scaled by 1 - d^2/r^2
	// and decaying linearly to zero over Duration. Shake.Frequency (CAMS float 1) is not read by the camera alter.
	const float Falloff = 1.f - FMath::Square(DistanceMetres / Shake.Range);
	FAddCameraShakeParams Params(Falloff * CVarEffectShakeScale.GetValueOnGameThread());
	Params.Initializer.BindLambda([Shake](UCameraShakeBase* Instance)
	{
		ULegacyCameraShake* Legacy = Cast<ULegacyCameraShake>(Instance);
		if (!Legacy)
		{
			return;
		}
		const float DurationSeconds = FMath::Max(Shake.Duration, MinShakeSeconds);
		Legacy->OscillationDuration = DurationSeconds;
		Legacy->OscillationBlendInTime = 0.f;
		Legacy->OscillationBlendOutTime = DurationSeconds;
		const float HalfAngleDegrees = FMath::RadiansToDegrees(Shake.Amplitude) * 0.5f;
		Legacy->RotOscillation.Pitch.Amplitude = HalfAngleDegrees;
		Legacy->RotOscillation.Pitch.Frequency = ShakeNoiseHertz;
		Legacy->RotOscillation.Yaw.Amplitude = HalfAngleDegrees;
		Legacy->RotOscillation.Yaw.Frequency = ShakeNoiseHertz;
	});
	Camera->StartCameraShake(ULegacyCameraShake::StaticClass(), Params);
}
