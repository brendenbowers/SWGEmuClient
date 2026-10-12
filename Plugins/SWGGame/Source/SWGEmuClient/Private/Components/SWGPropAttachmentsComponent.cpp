#include "Components/SWGPropAttachmentsComponent.h"

#include "Async/Async.h"
#include "Common/SWGLightingChannels.h"
#include "Common/SWGWorldScale.h"
#include "Common/SWGPropWind.h"
#include "Common/SWGRetailLight.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Objects/SWGObject.h"
#include "Objects/World/SWGStaticProp.h"
#include "SWGEffectsSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGPropAttachmentsSubsystem.h"

namespace
{
	// A light's colour floats are pre-scaled; the streetlamps top out at this, so it stands for brightness 1.
	constexpr float ReferenceLightComponent = 0.255f;
	constexpr float MinLightBrightness = 0.25f;
	constexpr float MaxLightBrightness = 4.f;

	/** Local transform of a cdf offset/heading (SWG axes, metres, degrees about up). */
	FTransform ToLocalTransform(const FVector3f& Offset, float HeadingDegrees)
	{
		return FTransform(FRotator(0.f, HeadingDegrees, 0.f), SWGFx::SimToUnreal(FVector(Offset)));
	}

	float MaxComponent(const FLinearColor& Color)
	{
		return FMath::Max3(Color.R, Color.G, Color.B);
	}

	FLinearColor Normalised(const FLinearColor& Color)
	{
		const float Peak = MaxComponent(Color);
		return Peak > KINDA_SMALL_NUMBER ? FLinearColor(Color.R / Peak, Color.G / Peak, Color.B / Peak) : FLinearColor::White;
	}
}

USWGPropAttachmentsComponent::USWGPropAttachmentsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = false;
}

void USWGPropAttachmentsComponent::Setup(USWGPropAttachmentsSubsystem& InOwner, const TSharedRef<const FSWGClientDataFile>& InData, bool bAllowObjectChildren)
{
	OwnerSubsystem = &InOwner;
	Data = InData;
	WindStrength = InOwner.GetWindStrength();

	for (const FSWGClientDataFlag& Flag : InData->Flags)
	{
		FSWGPropSkeletalSlot& Slot = SkeletalSlots.AddDefaulted_GetRef();
		Slot.SatPath = Flag.AppearancePath;
		Slot.Hardpoint = FName(*Flag.Hardpoint);
		Slot.WindScale = Flag.WindScale;
		Slot.MaxWindSpeed = Flag.MaxWindSpeed;
		Slot.Customization = InData->Customization;
	}

	for (const FSWGClientDataHardpointObject& Object : InData->HardpointObjects)
	{
		// The rest are ship parts (.apt) and ship controllers, out of scope for ground props.
		if (Object.AppearancePath.EndsWith(TEXT(".prt"), ESearchCase::IgnoreCase))
		{
			FSWGPropParticleSlot& Slot = ParticleSlots.AddDefaulted_GetRef();
			Slot.Path = Object.AppearancePath;
			Slot.Hardpoint = FName(*Object.Hardpoint);
		}
	}

	for (const FSWGClientDataLight& Light : InData->Lights)
	{
		const FTransform LightOffset(SWGFx::SimToUnreal(FVector(Light.Offset)));
		if (!Light.EffectPath.IsEmpty())
		{
			FSWGPropParticleSlot& Slot = ParticleSlots.AddDefaulted_GetRef();
			Slot.Path = Light.EffectPath;
			Slot.Offset = LightOffset;
			Slot.bNightOnly = Light.bNightOnly;
		}

		// A black light (the candlestick) or one without a range only has its glow.
		if (MaxComponent(Light.Color) > KINDA_SMALL_NUMBER && Light.Range > KINDA_SMALL_NUMBER)
		{
			FSWGPropLightSlot& Slot = LightSlots.AddDefaulted_GetRef();
			Slot.Offset = LightOffset;
			Slot.Color = Normalised(Light.Color);
			Slot.Brightness = FMath::Clamp(MaxComponent(Light.Color) / ReferenceLightComponent, MinLightBrightness, MaxLightBrightness);
			Slot.RangeCm = SWGToUnrealSpace(Light.Range);
			Slot.FalloffExponent = SWGRetailLight::FitFalloffExponent(Light.Params[10], Light.Params[11], Light.Params[12], Light.Range);
			Slot.bNightOnly = Light.bNightOnly;
			// Campfires and braziers carry a second colour and range that the light moves between.
			Slot.bFlicker = MaxComponent(Light.SecondColor) > KINDA_SMALL_NUMBER && Light.SecondRange > KINDA_SMALL_NUMBER;
			Slot.SecondColor = Slot.bFlicker ? Normalised(Light.SecondColor) : Slot.Color;
			Slot.SecondRangeCm = Slot.bFlicker ? SWGToUnrealSpace(Light.SecondRange) : Slot.RangeCm;
			Slot.FlickerPhase = FMath::FRand() * UE_TWO_PI;
		}
	}

	for (const FSWGClientDataChild& Child : InData->Children)
	{
		if (Child.Path.IsEmpty())
		{
			continue;
		}

		const FTransform ChildOffset = ToLocalTransform(Child.Offset, Child.HeadingDegrees);
		if (Child.bObjectTemplate)
		{
			if (bAllowObjectChildren)
			{
				SpawnObjectChild(Child);
			}
		}
		else if (Child.Path.EndsWith(TEXT(".prt"), ESearchCase::IgnoreCase))
		{
			FSWGPropParticleSlot& Slot = ParticleSlots.AddDefaulted_GetRef();
			Slot.Path = Child.Path;
			Slot.Offset = ChildOffset;
		}
		else if (Child.Path.EndsWith(TEXT(".sat"), ESearchCase::IgnoreCase))
		{
			FSWGPropSkeletalSlot& Slot = SkeletalSlots.AddDefaulted_GetRef();
			Slot.SatPath = Child.Path;
			Slot.Offset = ChildOffset;
		}
		else
		{
			FSWGPropMeshSlot& Slot = MeshSlots.AddDefaulted_GetRef();
			Slot.Path = Child.Path;
			Slot.Offset = ChildOffset;
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < MeshSlots.Num(); ++SlotIndex)
	{
		RequestMeshChild(SlotIndex);
	}

	InOwner.RegisterProp(this);
}

void USWGPropAttachmentsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Shutdown();
	Super::EndPlay(EndPlayReason);
}

void USWGPropAttachmentsComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Shutdown();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void USWGPropAttachmentsComponent::Shutdown()
{
	for (FSWGPropParticleSlot& Slot : ParticleSlots)
	{
		Slot.Handle.Stop(true);
		Slot.Handle = FSWGEffectHandle();
	}

	for (AActor* Child : ChildActors)
	{
		if (IsValid(Child))
		{
			Child->Destroy();
		}
	}
	ChildActors.Reset();

	if (USWGPropAttachmentsSubsystem* Subsystem = OwnerSubsystem.Get())
	{
		Subsystem->UnregisterProp(this);
	}
}

// -- Driven by the subsystem -------------------------------------------------

void USWGPropAttachmentsComponent::SetFlagsActive(bool bActive)
{
	if (bFlagsActive != bActive)
	{
		bFlagsActive = bActive;
		ReconcileFlags();
	}
}

void USWGPropAttachmentsComponent::SetEffectsActive(bool bActive)
{
	const bool bChanged = bEffectsActive != bActive;
	bEffectsActive = bActive;
	// While active it also restarts one-shot effects that finished.
	if (bChanged || bActive)
	{
		ReconcileParticles();
	}
	if (bChanged)
	{
		ReconcileLights();
	}
}

void USWGPropAttachmentsComponent::SetEnvironment(bool bInNight, float InWindStrength)
{
	const bool bNightChanged = bNight != bInNight;
	const bool bWindChanged = !FMath::IsNearlyEqual(WindStrength, InWindStrength, 0.01f);
	bNight = bInNight;
	WindStrength = InWindStrength;

	if (bNightChanged)
	{
		ReconcileParticles();
		ReconcileLights();
	}
	if (bWindChanged && bFlagsActive)
	{
		ReconcileFlags();
	}
}

void USWGPropAttachmentsComponent::SetLightsAllowed(bool bAllowed)
{
	if (bLightsAllowed != bAllowed)
	{
		bLightsAllowed = bAllowed;
		ReconcileLights();
	}
}

bool USWGPropAttachmentsComponent::WantsLight() const
{
	if (!bEffectsActive)
	{
		return false;
	}
	for (const FSWGPropLightSlot& Slot : LightSlots)
	{
		if (IsLit(Slot.bNightOnly))
		{
			return true;
		}
	}
	return false;
}

int32 USWGPropAttachmentsComponent::CountPlayingFlags() const
{
	int32 Count = 0;
	for (const FSWGPropSkeletalSlot& Slot : SkeletalSlots)
	{
		Count += (Slot.Mesh && Slot.bPoseReady && bFlagsActive) ? 1 : 0;
	}
	return Count;
}

int32 USWGPropAttachmentsComponent::CountLitLights() const
{
	int32 Count = 0;
	for (const FSWGPropLightSlot& Slot : LightSlots)
	{
		Count += (Slot.Light && Slot.Light->GetVisibleFlag()) ? 1 : 0;
	}
	return Count;
}

int32 USWGPropAttachmentsComponent::CountLiveParticles() const
{
	int32 Count = 0;
	for (const FSWGPropParticleSlot& Slot : ParticleSlots)
	{
		Count += Slot.Handle.IsActive() ? 1 : 0;
	}
	return Count;
}

// -- Reconciliation ------------------------------------------------------------

void USWGPropAttachmentsComponent::ReconcileFlags()
{
	for (int32 SlotIndex = 0; SlotIndex < SkeletalSlots.Num(); ++SlotIndex)
	{
		FSWGPropSkeletalSlot& Slot = SkeletalSlots[SlotIndex];
		if (bFlagsActive && !Slot.Mesh && !Slot.bFailed)
		{
			CreateSkeletal(Slot, SlotIndex);
		}
		if (!Slot.Mesh)
		{
			continue;
		}

		// A hidden flag stops evaluating its pose.
		Slot.Mesh->SetComponentTickEnabled(bFlagsActive);
		Slot.Mesh->SetVisibility(bFlagsActive && Slot.bPoseReady);
		if (bFlagsActive)
		{
			UpdateSkeletalClip(Slot, SlotIndex);
		}
	}
}

void USWGPropAttachmentsComponent::CreateSkeletal(FSWGPropSkeletalSlot& Slot, int32 SlotIndex)
{
	USWGMeshGeneratorSubsystem* Generator = GetMeshGenerator();
	USceneComponent* Root = GetRoot();
	if (!Generator || !Root || !Generator->ResolveSatLoopSource(Slot.SatPath, Slot.Source))
	{
		Slot.bFailed = true;
		return;
	}

	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(GetOwner(), NAME_None, RF_Transient);
	Mesh->SetupAttachment(Root);
	Mesh->SetRelativeTransform(ResolveLocal(Slot.Hardpoint, Slot.Offset));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCastShadow(false);
	Mesh->SetVisibility(false);
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->RegisterComponent();
	Slot.Mesh = Mesh;
}

void USWGPropAttachmentsComponent::UpdateSkeletalClip(FSWGPropSkeletalSlot& Slot, int32 SlotIndex)
{
	USWGMeshGeneratorSubsystem* Generator = GetMeshGenerator();
	if (!Generator || !Slot.Mesh || Slot.bBusy || Slot.bFailed)
	{
		return;
	}

	FString Clip;
	if (!Slot.Source.ClipPaths.IsEmpty())
	{
		const float Wind = SWGPropWind::ResolveFlagWind(WindStrength, Slot.WindScale, Slot.MaxWindSpeed);
		// A loop whose clips carry no wind names (a decorative droid) just plays its first.
		Clip = Slot.Source.ClipPaths[FMath::Max(0, SWGPropWind::SelectClipIndex(Slot.Source.ClipPaths, Wind))];
	}
	if (Slot.bPoseReady && Clip == Slot.PlayingClip)
	{
		return;
	}

	Slot.bBusy = true;
	Slot.PlayingClip = Clip;
	TWeakObjectPtr<USWGPropAttachmentsComponent> WeakThis(this);
	Generator->PlayLoopOnComponent(*Slot.Mesh, Slot.Source, Clip, Slot.Customization, [WeakThis, SlotIndex](bool bPlaying)
		{
			USWGPropAttachmentsComponent* Component = WeakThis.Get();
			if (!Component || !Component->SkeletalSlots.IsValidIndex(SlotIndex))
			{
				return;
			}
			FSWGPropSkeletalSlot& Done = Component->SkeletalSlots[SlotIndex];
			Done.bBusy = false;
			Done.bPoseReady = bPlaying;
			Done.bFailed = !bPlaying;
			if (Done.Mesh)
			{
				Done.Mesh->SetVisibility(bPlaying && Component->bFlagsActive);
			}
			// The wind may have moved while the clip was building.
			if (bPlaying && Component->bFlagsActive)
			{
				Component->UpdateSkeletalClip(Done, SlotIndex);
			}
		});
}

void USWGPropAttachmentsComponent::ReconcileParticles()
{
	USWGEffectsSubsystem* Effects = GetWorld() ? GetWorld()->GetSubsystem<USWGEffectsSubsystem>() : nullptr;
	for (FSWGPropParticleSlot& Slot : ParticleSlots)
	{
		if (bEffectsActive && Effects && IsLit(Slot.bNightOnly))
		{
			if (Slot.Handle.IsActive())
			{
				continue;
			}
			if (!Slot.Anchor)
			{
				Slot.Anchor = CreateAnchor(ResolveLocal(Slot.Hardpoint, Slot.Offset));
			}
			if (Slot.Anchor)
			{
				Slot.Handle = Effects->PlayParticleAttached(Slot.Path, Slot.Anchor);
			}
		}
		else if (Slot.Handle.IsValid())
		{
			Slot.Handle.Stop(true);
			Slot.Handle = FSWGEffectHandle();
		}
	}
}

void USWGPropAttachmentsComponent::ReconcileLights()
{
	const USWGPropAttachmentsSubsystem* Subsystem = OwnerSubsystem.Get();
	USceneComponent* Root = GetRoot();
	for (FSWGPropLightSlot& Slot : LightSlots)
	{
		const bool bOn = bEffectsActive && bLightsAllowed && IsLit(Slot.bNightOnly);
		if (bOn && !Slot.Light && Root && Subsystem)
		{
			UPointLightComponent* Light = NewObject<UPointLightComponent>(GetOwner(), NAME_None, RF_Transient);
			Light->SetupAttachment(Root);
			Light->SetRelativeTransform(Slot.Offset);
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetIntensityUnits(ELightUnits::Candelas);
			Light->SetCastShadows(false);
			Light->SetLightColor(Slot.Color);
			Light->SetAttenuationRadius(Slot.RangeCm);
			SWGRetailLight::ApplyFalloff(*Light, Slot.FalloffExponent);
			Light->SetIntensity(SWGRetailLight::ComputeIntensity(Slot.FalloffExponent, Slot.Brightness * Subsystem->GetLightIntensityScale(), Slot.RangeCm));
			if (IsInterior())
			{
				SWGSetInteriorLightingChannel(*Light);
			}
			Light->RegisterComponent();
			Slot.Light = Light;
		}
		if (Slot.Light && Slot.Light->GetVisibleFlag() != bOn)
		{
			Slot.Light->SetVisibility(bOn);
		}
	}
}

void USWGPropAttachmentsComponent::TickFlicker(float TimeSeconds)
{
	const USWGPropAttachmentsSubsystem* Subsystem = OwnerSubsystem.Get();
	if (!Subsystem)
	{
		return;
	}

	const float IntensityScale = Subsystem->GetLightIntensityScale();
	for (FSWGPropLightSlot& Slot : LightSlots)
	{
		if (!Slot.Light || !Slot.Light->GetVisibleFlag())
		{
			continue;
		}

		float RangeCm = Slot.RangeCm;
		if (Slot.bFlicker)
		{
			// Two unrelated sines read as a fire's irregular swell (the cdf's own timing floats are not decoded).
			const float Blend = 0.5f + 0.5f * (0.6f * FMath::Sin(TimeSeconds * 5.1f + Slot.FlickerPhase) + 0.4f * FMath::Sin(TimeSeconds * 11.3f + Slot.FlickerPhase * 1.7f));
			Slot.Light->SetLightColor(FLinearColor::LerpUsingHSV(Slot.Color, Slot.SecondColor, Blend));
			RangeCm = FMath::Lerp(Slot.RangeCm, Slot.SecondRangeCm, Blend);
			Slot.Light->SetAttenuationRadius(RangeCm);
		}
		SWGRetailLight::ApplyFalloff(*Slot.Light, Slot.FalloffExponent);
		Slot.Light->SetIntensity(SWGRetailLight::ComputeIntensity(Slot.FalloffExponent, Slot.Brightness * IntensityScale, RangeCm));
	}
}

// -- Children ------------------------------------------------------------------

void USWGPropAttachmentsComponent::RequestMeshChild(int32 SlotIndex)
{
	USWGMeshGeneratorSubsystem* Generator = GetMeshGenerator();
	if (!Generator || !MeshSlots.IsValidIndex(SlotIndex) || MeshSlots[SlotIndex].bRequested)
	{
		return;
	}
	MeshSlots[SlotIndex].bRequested = true;

	TWeakObjectPtr<USWGPropAttachmentsComponent> WeakThis(this);
	auto Place = [WeakThis, SlotIndex](UStaticMesh* Mesh, TArray<UMaterialInterface*> Materials)
	{
		USWGPropAttachmentsComponent* Component = WeakThis.Get();
		USceneComponent* Root = Component ? Component->GetRoot() : nullptr;
		if (!Mesh || !Root || !Component->IsRegistered() || !Component->MeshSlots.IsValidIndex(SlotIndex))
		{
			return;
		}

		FSWGPropMeshSlot& Slot = Component->MeshSlots[SlotIndex];
		UStaticMeshComponent* Child = NewObject<UStaticMeshComponent>(Component->GetOwner(), NAME_None, RF_Transient);
		Child->SetStaticMesh(Mesh);
		for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
		{
			Child->SetMaterial(MaterialIndex, Materials[MaterialIndex]);
		}
		// A static child cannot sit under a movable root.
		Child->SetMobility(Root->Mobility);
		Child->SetupAttachment(Root);
		Child->SetRelativeTransform(Slot.Offset);
		if (Component->IsInterior())
		{
			SWGSetInteriorLightingChannel(*Child, false);
		}
		Child->RegisterComponent();
		Slot.Mesh = Child;
	};

	// The generator hands results back on the game thread, but the failure path is not guaranteed to.
	Generator->RequestAppearanceMesh(MeshSlots[SlotIndex].Path, [Place](UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials)
		{
			if (IsInGameThread())
			{
				Place(Mesh, Materials);
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, [Place, Mesh, Materials]() { Place(Mesh, Materials); });
			}
		});
}

void USWGPropAttachmentsComponent::SpawnObjectChild(const FSWGClientDataChild& Child)
{
	UWorld* World = GetWorld();
	USceneComponent* Root = GetRoot();
	USWGMeshGeneratorSubsystem* Generator = GetMeshGenerator();
	if (!World || !Root || !Generator)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FTransform WorldTransform = ToLocalTransform(Child.Offset, Child.HeadingDegrees) * Root->GetComponentTransform();
	ASWGStaticProp* Prop = World->SpawnActor<ASWGStaticProp>(ASWGStaticProp::StaticClass(), WorldTransform, SpawnParams);
	if (!Prop)
	{
		return;
	}

	// Its own cdf is honoured (a garden's streetlamp lights), but it does not spawn grandchildren.
	Prop->Tags.Add(TEXT("SWGPropChild"));
	Prop->StaticTemplatePath = Child.Path;
	Prop->bInteriorLighting = IsInterior();
	ChildActors.Add(Prop);
	Generator->RequestMeshForTemplatePath(Prop, Child.Path);
}

// -- Helpers -------------------------------------------------------------------

USceneComponent* USWGPropAttachmentsComponent::GetRoot() const
{
	return GetOwner() ? GetOwner()->GetRootComponent() : nullptr;
}

USWGMeshGeneratorSubsystem* USWGPropAttachmentsComponent::GetMeshGenerator() const
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
}

FTransform USWGPropAttachmentsComponent::ResolveLocal(FName Hardpoint, const FTransform& Offset) const
{
	FTransform HardpointTransform = FTransform::Identity;
	if (!Hardpoint.IsNone())
	{
		const ASWGObject* Object = Cast<ASWGObject>(GetOwner());
		if (const FTransform* Found = Object ? Object->MeshHardpoints.Find(Hardpoint) : nullptr)
		{
			HardpointTransform = *Found;
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("USWGPropAttachmentsComponent: %s has no hardpoint '%s'; using its origin"), *GetNameSafe(GetOwner()), *Hardpoint.ToString());
		}
	}
	return Offset * HardpointTransform;
}

USceneComponent* USWGPropAttachmentsComponent::CreateAnchor(const FTransform& Local) const
{
	USceneComponent* Root = GetRoot();
	if (!Root)
	{
		return nullptr;
	}
	USceneComponent* Anchor = NewObject<USceneComponent>(GetOwner(), NAME_None, RF_Transient);
	Anchor->SetupAttachment(Root);
	Anchor->SetRelativeTransform(Local);
	Anchor->RegisterComponent();
	return Anchor;
}

bool USWGPropAttachmentsComponent::IsLit(bool bNightOnly) const
{
	return !bNightOnly || bNight || IsInterior();
}

bool USWGPropAttachmentsComponent::IsInterior() const
{
	const ASWGStaticProp* Prop = Cast<ASWGStaticProp>(GetOwner());
	return Prop && Prop->bInteriorLighting;
}
