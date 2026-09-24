#pragma once

#include "CoreMinimal.h"
#include "PreviewScene.h"

class UGameInstance;
class UDynamicMeshComponent;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTexture2D;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class USWGTreSubsystem;
class USWGMeshGeneratorSubsystem;

namespace UE::Geometry { class FDynamicMesh3; }

/** Orbit camera over a planet, Google-Maps style. Raw space (metres, x east, y north). */
struct SWGEMUCLIENT_API FSWGPlanetMapCamera
{
	/** Ground point the camera orbits. */
	FVector2D Target = FVector2D::ZeroVector;

	/** Metres from Target to the eye. */
	float Distance = 36000.f;

	/** Eye distance that shows the whole map straight down: the zoom-out limit and the top of the pitch curve. */
	float OverviewDistance = 36000.f;

	/** 0 looks north; positive turns toward east. */
	float Yaw = 0.f;

	/** Degrees added to the zoom-driven pitch (see GetPitch); the player's own tilt. */
	float Tilt = 0.f;

	static constexpr float MinDistance = 180.f;

	/** Whole-map overview for a map MapSize metres across (50° wide view, some margin). */
	static float OverviewDistanceFor(float MapSize) { return FMath::Max(MinDistance * 4.f, MapSize * 2.2f); }

	/** Straight down when zoomed out, leaning toward the horizon as it closes in. */
	float GetPitch() const;

	/** Shortest-path, log-distance blend used for the fly-to animation. */
	static FSWGPlanetMapCamera Blend(const FSWGPlanetMapCamera& From, const FSWGPlanetMapCamera& To, float Alpha);

	bool IsNearlyEqual(const FSWGPlanetMapCamera& Other) const;
};

/**
 * A 3D planet map: a preview world (not the game world, so any planet can be
 * shown from anywhere) holding a coarse whole-planet terrain mesh and the
 * lowest LOD of the snapshot buildings around given focus points, captured
 * on demand into a render target. USWGPlanetMapWidget is its UI.
 */
class SWGEMUCLIENT_API FSWGPlanetMapScene : public FGCObject, public TSharedFromThis<FSWGPlanetMapScene>
{
public:
	explicit FSWGPlanetMapScene(UGameInstance* GameInstance);
	virtual ~FSWGPlanetMapScene() override;

	/** Loads Planet (terrain only when it changes) and the buildings near FocusPoints. */
	void ShowPlanet(const FString& Planet, const TArray<FVector2D>& FocusPoints);

	bool IsTerrainReady() const { return !Heights.IsEmpty(); }
	float GetMapSize() const { return MapSize; }

	/** Terrain height under a raw point from the coarse grid; 0 until the terrain is ready. */
	float GetGroundHeight(const FVector2D& RawPoint) const;

	void SetViewportSize(const FIntPoint& Size);
	FIntPoint GetViewportSize() const { return ViewportSize; }
	void SetCamera(const FSWGPlanetMapCamera& Camera);
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

	/** Raw point to render-target pixels; false when behind the eye. */
	bool Project(const FVector& RawPoint, FVector2D& OutPixel) const;

	/** Render-target pixel onto the horizontal plane through the camera target; false above the horizon. */
	bool Deproject(const FVector2D& Pixel, FVector2D& OutRawPoint) const;

	/** Captures if the view or content changed. Game thread. */
	void RenderIfDirty();

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FSWGPlanetMapScene"); }

private:
	struct FTerrainBake
	{
		TArray<FColor> Pixels;
		/** GridSize x GridSize, rows running south from the north edge. */
		TArray<float> Heights;
		TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> Mesh;
		float MapSize = 0.f;
	};

	struct FBuildingPlacement
	{
		FString TemplatePath;
		FVector Position = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
	};

	void ClearTerrain();
	void ClearBuildings();
	void ApplyTerrain(FTerrainBake&& Bake);
	void RequestBuildings(TArray<FBuildingPlacement>&& Placements);
	FVector EyeLocation() const;
	FRotator EyeRotation() const;
	float FocalLengthPixels() const;

	static FTerrainBake BakeTerrain(TArray<uint8>&& TerrainBytes, const FString& PlanetName);
	static TArray<FBuildingPlacement> SelectBuildings(TArray<uint8>&& SnapshotBytes, const TArray<FVector2D>& Points);

	FPreviewScene PreviewScene;

	TObjectPtr<USWGTreSubsystem> Tre;
	TObjectPtr<USWGMeshGeneratorSubsystem> MeshGenerator;
	TObjectPtr<USceneCaptureComponent2D> Capture;
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;
	TObjectPtr<UDynamicMeshComponent> TerrainComponent;
	TObjectPtr<UTexture2D> ReliefTexture;
	TObjectPtr<UMaterialInstanceDynamic> TerrainMaterial;
	TArray<TObjectPtr<UStaticMeshComponent>> BuildingComponents;

	FString Planet;
	TArray<FVector2D> FocusPoints;
	/** Bumped per load so late bakes and mesh callbacks for what was replaced are dropped. */
	int32 TerrainGeneration = 0;
	int32 BuildingGeneration = 0;
	TArray<float> Heights;
	float MapSize = 16384.f;
	FIntPoint ViewportSize = FIntPoint(740, 380);
	FSWGPlanetMapCamera Camera;
	bool bDirty = true;
	int32 RemainingSettleFrames = 0;
};
