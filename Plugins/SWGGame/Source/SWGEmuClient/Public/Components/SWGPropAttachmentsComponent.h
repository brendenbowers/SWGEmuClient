#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWGEffectsTypes.h"
#include "Common/SWGSatLoopSource.h"
#include "TRE/SWGClientDataFileReader.h"
#include "SWGPropAttachmentsComponent.generated.h"

class UPointLightComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class USWGMeshGeneratorSubsystem;
class USWGPropAttachmentsSubsystem;

/** A cloth .sat (flag, banner) or decorative skeletal child, placed at a hardpoint or offset. */
USTRUCT()
struct FSWGPropSkeletalSlot
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh;

	FString SatPath;
	FName Hardpoint;
	FTransform Offset = FTransform::Identity;
	TMap<FString, int32> Customization;
	float WindScale = 1.f;
	float MaxWindSpeed = 10.f;

	FSWGSatLoopSource Source;
	FString PlayingClip;
	bool bFailed = false;
	bool bPoseReady = false;
	bool bBusy = false;
};

/** A looping .prt at a hardpoint or offset (HOBJ, an LOBJ glow, a CHLD .prt). */
USTRUCT()
struct FSWGPropParticleSlot
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Anchor;

	FString Path;
	FName Hardpoint;
	FTransform Offset = FTransform::Identity;
	bool bNightOnly = false;
	FSWGEffectHandle Handle;
};

/** An LOBJ point light. Colour and range are normalised once in Setup; see FSWGClientDataLight for the fields. */
USTRUCT()
struct FSWGPropLightSlot
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> Light;

	FTransform Offset = FTransform::Identity;
	FLinearColor Color = FLinearColor::White;
	FLinearColor SecondColor = FLinearColor::White;
	float RangeCm = 800.f;
	float SecondRangeCm = 800.f;
	float Brightness = 1.f;
	/** UE falloff exponent fitted to the retail constant/linear/quadratic attenuation over the light's range. */
	float FalloffExponent = 2.f;
	float FlickerPhase = 0.f;
	bool bNightOnly = false;
	bool bFlicker = false;
};

/** A CHLD appearance placed relative to the prop. */
USTRUCT()
struct FSWGPropMeshSlot
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Mesh;

	FString Path;
	FTransform Offset = FTransform::Identity;
	bool bRequested = false;
};

/**
 * Everything a template's .cdf adds to a prop: wind-driven flags and banners, particle and light objects, child
 * appearances and child object templates. It never ticks: USWGPropAttachmentsSubsystem decides, a few times a second,
 * what is near enough to run (flags, particles, lights) and calls the setters; children are placed once.
 * Added to props by that subsystem once the prop's mesh lands.
 */
UCLASS(ClassGroup = (SWG))
class SWGEMUCLIENT_API USWGPropAttachmentsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGPropAttachmentsComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	/** Builds the slots from the parsed cdf and places the children. bAllowObjectChildren is false for a prop that is itself a CHL2 child. */
	void Setup(USWGPropAttachmentsSubsystem& InOwner, const TSharedRef<const FSWGClientDataFile>& InData, bool bAllowObjectChildren);

	// -- Driven by the subsystem ------------------------------------------

	void SetFlagsActive(bool bActive);
	void SetEffectsActive(bool bActive);
	/** Night and wind strength (0..1); flags re-pick their clip when the wind crosses a tier. */
	void SetEnvironment(bool bNight, float InWindStrength);
	void SetLightsAllowed(bool bAllowed);
	/** Moves a lit light's colour and range along its flicker. */
	void TickFlicker(float TimeSeconds);

	bool IsFlagsActive() const { return bFlagsActive; }
	bool IsEffectsActive() const { return bEffectsActive; }
	/** Effects are running and at least one light is lit right now, so the light budget should count it. */
	bool WantsLight() const;

	int32 CountPlayingFlags() const;
	int32 CountLitLights() const;
	int32 CountLiveParticles() const;

private:
	void Shutdown();
	void ReconcileFlags();
	void ReconcileParticles();
	void ReconcileLights();
	void CreateSkeletal(FSWGPropSkeletalSlot& Slot, int32 SlotIndex);
	void UpdateSkeletalClip(FSWGPropSkeletalSlot& Slot, int32 SlotIndex);
	void RequestMeshChild(int32 SlotIndex);
	void SpawnObjectChild(const FSWGClientDataChild& Child);

	USceneComponent* GetRoot() const;
	FTransform ResolveLocal(FName Hardpoint, const FTransform& Offset) const;
	USceneComponent* CreateAnchor(const FTransform& Local) const;
	bool IsLit(bool bNightOnly) const;
	bool IsInterior() const;

	USWGMeshGeneratorSubsystem* GetMeshGenerator() const;

	UPROPERTY(Transient)
	TArray<FSWGPropSkeletalSlot> SkeletalSlots;

	UPROPERTY(Transient)
	TArray<FSWGPropParticleSlot> ParticleSlots;

	UPROPERTY(Transient)
	TArray<FSWGPropLightSlot> LightSlots;

	UPROPERTY(Transient)
	TArray<FSWGPropMeshSlot> MeshSlots;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> ChildActors;

	TWeakObjectPtr<USWGPropAttachmentsSubsystem> OwnerSubsystem;
	TSharedPtr<const FSWGClientDataFile> Data;

	bool bFlagsActive = false;
	bool bEffectsActive = false;
	bool bLightsAllowed = false;
	bool bNight = false;
	float WindStrength = 0.5f;
};
