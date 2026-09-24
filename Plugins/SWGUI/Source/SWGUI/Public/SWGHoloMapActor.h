#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWGMapMarkerWidget.h"
#include "SWGHoloMapActor.generated.h"

class UDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

namespace UE::Geometry { class FDynamicMesh3; }

/**
 * A hologram of the ground around a point, projected in the world: a round
 * patch of real terrain (planet data plus building pads), the lowest LOD of
 * the snapshot buildings on it and marker beams, all in M_SWGHologram.
 *
 * Pan, turn and zoom apply at once by moving and scaling the content under
 * the fixed disc (the material fades anything past its rim); the patch is
 * rebaked on a worker once the view has drifted or settled. Raw space is
 * metres, x east, y north, as everywhere else.
 */
UCLASS(NotPlaceable)
class SWGUI_API ASWGHoloMapActor : public AActor
{
	GENERATED_BODY()

public:
	ASWGHoloMapActor();

	/** The raw point shown at the disc's centre. */
	void SetViewCenter(const FVector2D& RawCenter);
	FVector2D GetViewCenter() const { return ViewCenter; }

	/** Metres from centre to rim. */
	void SetViewRadius(float RawRadius);
	float GetViewRadius() const { return ViewRadius; }

	/** Turns the content about the disc's centre, degrees. */
	void SetViewYaw(float Degrees);
	float GetViewYaw() const { return ViewYaw; }

	/** Replaces one layer of beams. Markers use FSWGMapMarker as the 2D map does; Style "Player" draws a heading arrow. */
	void SetMarkers(FName Layer, const TArray<FSWGMapMarker>& Markers);

	/** Where a world-space ray meets the hologram's ground plane, in raw metres. */
	bool RayToRaw(const FVector& Origin, const FVector& Direction, FVector2D& OutRaw) const;

	/** World position of the disc's centre at terrain level, for the camera to look at. */
	FVector GetFocusLocation() const;

	/** Which side of the disc the projector droid hovers over (world XY); the far side from the viewer keeps it out of the way. */
	void SetDroidSide(const FVector2D& Direction);

	/** The hovering droid that projects the image; a mobile template, loaded through the item mesh path. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap")
	FString DroidTemplate = TEXT("object/mobile/shared_training_remote.iff");

	/** Droid height above the disc, as a fraction of the disc's radius. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap")
	float DroidHeight = 0.5f;

	/** The training remote's model is about 10 cm across; scaled up so it reads at arm's length. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap")
	float DroidScale = 2.5f;

	/** How far past the centre toward DroidSide it hovers, as a fraction of the disc's radius. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap")
	float DroidReach = 0.75f;

	/** Disc diameter in world units. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap")
	float DiscDiameter = 240.f;

	/** Seconds between looks for server-sent structures (player houses, installations) arriving or leaving. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap")
	float DynamicStructureScanSeconds = 3.f;

	/** Height relief multiplier; real relief at arm's length reads as flat. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap")
	float HeightExaggeration = 2.f;

	/** Metres between contour lines at the default radius; scales with zoom. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap")
	float ContourMetres = 20.f;

	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap")
	FLinearColor HoloColor = FLinearColor(0.12f, 0.55f, 1.f);

	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap")
	float HoloIntensity = 0.7f;

	static constexpr float MinRadius = 250.f;
	static constexpr float MaxRadius = 6000.f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
public:
	/** A baked patch: GridSize x GridSize heights over 2 x Extent, rows running north. */
	struct FTerrainBake
	{
		TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> Mesh;
		TArray<float> Heights;
		float BaseHeight = 0.f;
	};

private:
	struct FMarkerVisual
	{
		FSWGMapMarker Marker;
		TObjectPtr<UStaticMeshComponent> Beam;
		TObjectPtr<UTextRenderComponent> Label;
	};

	void RequestBake();
	void ApplyBake(FTerrainBake&& Bake, const FVector2D& Center, float Radius);
	void RequestBuildings();
	void ClearBuildings();

	/**
	 * Mirrors server-sent buildings and installations (anything not from the
	 * .ws snapshot) that are in memory near the view, reusing the meshes their
	 * actors already built. bRebuild re-lays every one after a rebake.
	 */
	void RefreshDynamicStructures(bool bRebuild);
	void AddDynamicStructure(AActor& Structure);
	void RemoveDynamicStructure(const TWeakObjectPtr<AActor>& Structure);
	void UpdateContentTransform();
	void UpdateMarkers();
	UMaterialInstanceDynamic* MakeHoloMaterial(const FLinearColor& Color, float Intensity);

	/** Raw offset from the baked centre to content-local units. */
	FVector RawToContent(const FVector& RawOffset) const;
	float BakedHeightAt(const FVector2D& Raw) const;
	float BakedUnitsPerMetre() const { return DiscDiameter * 0.5f / BakedRadius; }

	UPROPERTY()
	TObjectPtr<USceneComponent> ContentRoot;

	UPROPERTY()
	TObjectPtr<UDynamicMeshComponent> TerrainComponent;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BaseComponent;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> GlowLight;

	/** Bobs and turns in Tick; carries whichever mesh component the droid's template resolves to. */
	UPROPERTY()
	TObjectPtr<USceneComponent> DroidRoot;

	/** A faint cone of light from the droid down onto the disc. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ProjectionBeam;

	void RequestDroid();
	void UpdateDroid();

	FVector2D DroidSide = FVector2D(1.f, 0.f);
	float DroidTime = 0.f;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> HoloMaterial;

	/** Shared by terrain and buildings. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ContentMaterial;

	/** Every hologram material this actor made; their Center/Fade follow the actor each tick. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> HoloMaterials;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> BuildingComponents;

	/** Hologram copies of each mirrored structure's mesh components; the components themselves are owned by this actor. */
	TMap<TWeakObjectPtr<AActor>, TArray<TWeakObjectPtr<UStaticMeshComponent>>> DynamicStructures;
	double NextDynamicScanTime = 0.0;

	UPROPERTY()
	TObjectPtr<UStaticMesh> BeamMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ArrowMesh;

	TMap<FName, TArray<FMarkerVisual>> MarkerLayers;
	/** Keeps the marker components alive; FMarkerVisual isn't reflected. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> MarkerObjects;

	FVector2D ViewCenter = FVector2D::ZeroVector;
	float ViewRadius = 2500.f;
	float ViewYaw = 0.f;

	/** What the content was built for; the content transform bridges to the live view. */
	FVector2D BakedCenter = FVector2D::ZeroVector;
	float BakedRadius = 2500.f;
	float BakedBaseHeight = 0.f;
	TArray<float> BakedHeights;
	bool bHasBake = false;
	bool bBakeInFlight = false;
	bool bBakeWanted = false;
	int32 BakeGeneration = 0;
	int32 BuildingGeneration = 0;
	double LastViewChangeTime = 0.0;
	float FadeAlpha = 0.f;
};
