#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWGSpaceNebulaComponent.generated.h"

class USWGTreSubsystem;
class UHeterogeneousVolumeComponent;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class APostProcessVolume;
class ADirectionalLight;

/** The space scene's gas nebulas, one heterogeneous volume per row of the zone's nebula table. Owned by the sky actor. */
UCLASS(ClassGroup = "SWGEmu")
class SWGEMUCLIENT_API USWGSpaceNebulaComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	struct FNebulaVisual
	{
		TObjectPtr<UHeterogeneousVolumeComponent> Volume;
		FVector SpacePosition;
		double Radius;
		TObjectPtr<UMaterialInstanceDynamic> Material;
		/** Lit-gas colour blended toward the facing colour, used for the inside-cloud tint and wisps. */
		FLinearColor Tint = FLinearColor::White;
		float Density = 0;
		float LightningFrequency = 0;
		float LightningDurationMax = 0;
		FLinearColor LightningColor = FLinearColor::White;
		FLinearColor LightningRampColor = FLinearColor::White;
		bool bFar = false;
	};

	/** Spawns the volumes for datatables/space/nebula/<zone>.iff, if the zone has one. False on a bad table or missing material. */
	bool Setup(USWGTreSubsystem* TreSubsystem, const FString& TerrainVirtualPath);

	/** Keeps every volume on its absolute space position, then applies quality, distance LOD, the inside-cloud effects and lightning. Viewpoint is the camera in world space. */
	void Update(const FVector& Viewpoint, const TArray<TObjectPtr<ADirectionalLight>>& Lights, float DeltaTime);

	/** Destroys the volume actors, which are separate from the sky actor, and restores the renderer settings it changed. */
	void Teardown();

	/** Starts a flash inside the nebula now, if it has lightning authored and a flash light is free. */
	bool TriggerLightning(int32 NebulaIndex);

	/** Volume resolution for a quality tier (0-3) and LOD state. */
	static FIntVector GetVolumeResolution(int32 Quality, bool bFarLod);

	/** 0 outside every nebula, rising to 1 deep inside the densest one the camera is in. */
	float GetInsideFactor() const { return InsideFactor; }

	TArray<FNebulaVisual> Nebulas;

	/** Weighted unbound volume that tints the view inside a cloud; its BlendWeight is the inside factor. */
	UPROPERTY()
	TObjectPtr<APostProcessVolume> InsideVolume;

	/** Camera-facing soft quads around the camera, fixed in absolute space so they stream past as the ship moves. */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Wisps;

	UPROPERTY()
	TArray<TObjectPtr<UPointLightComponent>> FlashLights;

private:
	struct FFlash
	{
		int32 NebulaIndex = INDEX_NONE;
		float Age = 0;
		float Duration = 0;
		int32 LightIndex = INDEX_NONE;
	};

	void ApplyVolumeSettings(FNebulaVisual& Nebula, int32 Quality);
	void ApplyQuality();
	void ApplyDistanceLod(FNebulaVisual& Nebula, double Distance);
	void UpdateInsideCloud(const FVector& Camera);
	void UpdateWisps(const FTransform& Frame, const FVector& Camera);
	void UpdateLightning(const FVector& Camera, float DeltaTime);

	int32 AppliedQuality = INDEX_NONE;
	float InsideFactor = 0;
	int32 InsideIndex = INDEX_NONE;
	float BaseStarIntensity = -1;
	bool bStarsDimmed = false;
	bool bLightScatterApplied = false;
	TWeakObjectPtr<UMaterialInstanceDynamic> StarMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> WispMaterial;
	TArray<FVector> WispOffsets;
	TArray<FFlash> Flashes;
	FRandomStream Random{2026};
	TMap<FString, FString> OriginalRenderSettings;
};
