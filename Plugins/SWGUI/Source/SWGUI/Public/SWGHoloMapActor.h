#pragma once

#include "CoreMinimal.h"
#include "SWGHoloProjectorActor.h"
#include "SWGMapMarkerWidget.h"
#include "SWGHoloMapActor.generated.h"

class UDynamicMeshComponent;
class UTextRenderComponent;

namespace UE::Geometry { class FDynamicMesh3; }

/**
 * A hologram of the ground around a point, projected in the world: a round
 * patch of real terrain (planet data plus building pads), the lowest LOD of
 * the snapshot buildings on it and marker beams, all in M_SWGHologram. The
 * droid's rays land on the disc's rim and follow the content round as it turns.
 *
 * Pan, turn and zoom apply at once by moving and scaling the content under
 * the fixed disc (the material fades anything past its rim); the patch is
 * rebaked on a worker once the view has drifted or settled. Raw space is
 * metres, x east, y north, as everywhere else.
 */
UCLASS(NotPlaceable)
class SWGUI_API ASWGHoloMapActor : public ASWGHoloProjectorActor
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
	bool RayToRaw(const FVector& Origin, const FVector& Direction, FVector2D& OutRaw, bool bRequireOnDisc = true) const;

	/** World position of the disc's centre at terrain level, for the camera to look at. */
	FVector GetFocusLocation() const;

	/** Seconds between looks for server-sent structures (player houses, installations) arriving or leaving. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap")
	float DynamicStructureScanSeconds = 3.f;

	/** Buildings shrink about their own origin so crowded cities read inside the disc; positions stay true. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap", meta = (ClampMin = "0.1", ClampMax = "1"))
	float BuildingScale = 0.8f;

	/** Height relief multiplier; real relief at arm's length reads as flat. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap")
	float HeightExaggeration = 2.f;

	/** Metres between contour lines at the default radius; scales with zoom. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|HoloMap")
	float ContourMetres = 20.f;

	static constexpr float MinRadius = 100.f;
	static constexpr float MaxRadius = 6000.f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float GetDroidExtraYaw() const override { return ViewYaw; }
	virtual float GetRimAngleOffset() const override { return ProjectionYawOffset; }

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
		/** Player only: a tall locator beam and a ground ring that ripples outward. */
		TObjectPtr<UStaticMeshComponent> Locator;
		TObjectPtr<UStaticMeshComponent> Ping;
		TObjectPtr<UMaterialInstanceDynamic> PingMaterial;
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

	/** Raw offset from the baked centre to content-local units. */
	FVector RawToContent(const FVector& RawOffset) const;
	float BakedHeightAt(const FVector2D& Raw) const;
	float BakedUnitsPerMetre() const { return DiscDiameter * 0.5f / BakedRadius; }

	UPROPERTY()
	TObjectPtr<USceneComponent> ContentRoot;

	UPROPERTY()
	TObjectPtr<UDynamicMeshComponent> TerrainComponent;

	/** Keeps the rays on the same patch of ground while the content turns; eases back once it stops. */
	float ProjectionYawOffset = 0.f;

	/** Shared by terrain and buildings. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ContentMaterial;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> BuildingComponents;

	/** Hologram copies of each mirrored structure's mesh components; the components themselves are owned by this actor. */
	TMap<TWeakObjectPtr<AActor>, TArray<TWeakObjectPtr<UStaticMeshComponent>>> DynamicStructures;
	double NextDynamicScanTime = 0.0;

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
};
