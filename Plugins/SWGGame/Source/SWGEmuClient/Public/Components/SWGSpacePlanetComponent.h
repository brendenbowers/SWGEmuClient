#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Async/Future.h"
#include "HAL/ThreadSafeBool.h"
#include "SWGSpacePlanetComponent.generated.h"

struct FSWGSpaceTerrainData;
class USWGTreSubsystem;
class USWGMeshGeneratorSubsystem;
class UDynamicMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
struct FSWGTerrainBakeResult;
struct FSWGPlanetGlobe;
struct FSWGPlanetGlobeParams;
class ADirectionalLight;
class UDirectionalLightComponent;

/** The space scene's planets (surface, clouds, atmosphere shell or halo) and celestial sprites. Owned by the sky actor. */
UCLASS(ClassGroup = "SWGEmu")
class SWGEMUCLIENT_API USWGSpacePlanetComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Client art for one planet's atmosphere (not retail data); see FindAtmosphereStyle. */
	struct FAtmosphereStyle
	{
		FLinearColor Color = FLinearColor(.5f, .6f, .8f);
		FLinearColor TerminatorColor = FLinearColor(1.f, .5f, .25f);
		float Density = .5f;
		float NightRim = .06f;
		/** Sun glint on dark albedo (water). */
		bool bOcean = false;
		/** Retail halo sprite as an outer glow: 0 drops it, scale multiplies the authored size. */
		float HaloIntensity = .5f;
		float HaloScale = .8f;
	};

	struct FPlanetVisual
	{
		TObjectPtr<UDynamicMeshComponent> Surface;
		TObjectPtr<UDynamicMeshComponent> Clouds;
		TObjectPtr<UDynamicMeshComponent> Atmosphere;
		TObjectPtr<UStaticMeshComponent> Halo;
		/** Null when the layer uses a generic object material without the Stage 4 parameters. */
		TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;
		TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;
		TObjectPtr<UMaterialInstanceDynamic> AtmosphereMaterial;
		float SurfaceSpin = 0;
		float CloudSpin = 0;
		float HaloRoll = 0;
	};
	struct FCelestialVisual
	{
		TObjectPtr<UStaticMeshComponent> Sprite;
		/** Retail celestial size (the quad spans this many radians). */
		float Size = 0;
		/** Code-built star (disc and corona) standing in for a retail sprite whose texture is a four-point cross; null for every other sprite. */
		TObjectPtr<UMaterialInstanceDynamic> StarMaterial;
		/** The retail material the star replaced, kept so a capture can show the difference. */
		TObjectPtr<UMaterialInstanceDynamic> RetailMaterial;
		/** The brightest star of the zone: lit with the key light's colour. */
		bool bMainSun = false;
	};

	/** Retail celestial shaders whose texture is a thin four-point cross (shader/cels_star_back and shader/starglow); the Stage 10 sun draws these as a disc and corona. */
	static bool IsCrossFlareShader(const FString& ShaderName);
	/** The main sun's index in Celestials, or INDEX_NONE when the zone draws none with the code-built material. */
	int32 GetMainSunIndex() const { return MainSunIndex; }

	/** Creates a camera-facing sky sprite under the sky actor; the subsystem's day/night sky shares it. */
	using FCreateSkySprite = TFunctionRef<UStaticMeshComponent*(const FString& ShaderName, bool bAdditive)>;

	/**
	 * Background layer distances from the camera, in cm, nearest first. Retail draws planets behind
	 * the world, so the nearest planet surface sits past the ±8 km zone's 28 km diagonal.
	 */
	static constexpr double PlanetNearestDistance = 3.0e6;
	static constexpr double StarDomeRadius = 1.2e8;
	static constexpr double CelestialDistance = 1.5e8;
	static constexpr double SkyboxDistance = 2.0e8;

	/**
	 * Spawns every STAT planet and celestial. SceneLights are the zone's PARA lights; planets get
	 * shadowless copies on lighting channel 1 so ship shadows never land on them. False if any asset is missing.
	 */
	bool Setup(const FSWGSpaceTerrainData& Data, USWGTreSubsystem* TreSubsystem, USWGMeshGeneratorSubsystem* MeshGenerator,
		FCreateSkySprite CreateSkySprite, TConstArrayView<ADirectionalLight*> SceneLights, const FString& TerrainVirtualPath = FString());

	/**
	 * The ground zone on the zone's primary planet (Stages 5 and 9): equirectangular globe maps around the cap centre, a high-resolution
	 * cap map plus a whole-globe map. The bake runs on a worker, so a region stays pending for a while after Setup.
	 */
	struct FRegionState
	{
		/** Index into Planets; INDEX_NONE when the zone's planet has no ground terrain. */
		int32 PlanetIndex = INDEX_NONE;
		FString GroundTerrain;
		bool bPending = false;
		bool bReady = false;
		/** The ground bake and the globe both came from the cache. */
		bool bFromCache = false;
		bool bGlobeFromCache = false;
		/** The retail overhead map stands in for a failed bake. */
		bool bFallbackMap = false;
		/** Seconds the ground bake took (original bake on a cache hit), the globe composition, and the whole worker load. */
		double BakeSeconds = 0;
		double GlobeSeconds = 0;
		double LoadSeconds = 0;
		/** Radius of the cap of the surface the viewer sees, radians: acos(surface radius / viewer distance). */
		float CapRadius = 0;
		/** Half width of the cap map, radians, in longitude and latitude. */
		float CapHalfWidth = 0;
		/** The playable map's half width as a fraction of CapRadius. */
		float Fill = 1;
		int32 CapResolution = 0;
		/** Pixels along the ground bake's edge (4096 at the top tier, 4 m a pixel) and the detail families that got textures. */
		int32 GroundResolution = 0;
		int32 DetailFamilyCount = 0;
		/** Raw ground units per radian of the globe; detail tiles per radian is this over swg.Space.PlanetDetailTile. */
		float GroundPerRadian = 0;
		/** Unit vector from the planet toward the viewer, in the surface component's object space. */
		FVector Centre = FVector::ForwardVector;
		FLinearColor AverageColour = FLinearColor::Gray;
		FString CacheBasePath;
		FString GlobeCacheBasePath;
	};
	const FRegionState& GetRegion() const { return Region; }
	UTexture2D* GetRegionColourTexture() const { return RegionColourTexture; }
	UTexture2D* GetRegionNormalTexture() const { return RegionNormalTexture; }
	UTexture2D* GetGlobeFarTexture() const { return RegionFarTexture; }
	UTexture2D* GetRegionDetailTexture() const { return RegionDetailTexture; }

	/** Radius of the surface cap seen from a viewer at ViewerDistance from the planet centre, radians; both in the same units. */
	static float ComputeCapRadius(float SurfaceRadius, float ViewerDistance);

	/** Where baked regions are cached. */
	static FString GetRegionCacheDirectory();

	virtual void BeginDestroy() override;

	/**
	 * Spins surfaces and clouds, turns halos and celestial sprites to face Viewpoint (world space), mirrors the scene lights and
	 * hands the first one to every planet material as SunDirection / SunRadiance.
	 */
	void Update(float DeltaTime, const FVector& Viewpoint);

	/** The art table entry for a STAT planet appearance path (e.g. appearance/planet_naboo.pln); a neutral blue for unknown ones. */
	static FAtmosphereStyle FindAtmosphereStyle(const FString& Appearance);

	/** Atmosphere shell radius over the surface radius; the same value as the material's ShellRatio. */
	static constexpr float AtmosphereShellRatio = .98f;

	/** Fill (non-key) PARA lights are dimmed on planets so the night side reads as night; the retail fill is nearly as strong as the key. */
	static constexpr float PlanetFillScale = .03f;
	/** How far the fill light is pulled from its authored colour toward a neutral cool grey (1 = fully). */
	static constexpr float PlanetFillDesaturation = .85f;

	/** World direction from the planets toward the key light (the first PARA light), valid after Update. */
	FVector GetSunDirection() const { return SunDirection; }
	bool HasSun() const { return bHasSun; }
	/** Key light colour * intensity / PI: the radiance Unreal's Lambert diffuse gives a white surface facing it. */
	FLinearColor GetSunRadiance() const { return SunRadiance; }

	TArray<FPlanetVisual> Planets;
	TArray<FCelestialVisual> Celestials;

	/** Uniform scale applied to STAT planet positions and radii; keeps each planet's angular size. */
	double BackgroundScale = 1;

private:
	friend class FSWGZoneSceneReadinessTest;
	/** Starts the worker bake and globe composition for Planets[PlanetIndex]; DetailPaths are its retail detail textures (for the brightness match). */
	void BeginRegionBake(int32 PlanetIndex, const FString& GroundTerrain, const TArray<FString>& DetailPaths, const FSWGPlanetGlobeParams& GlobeParams, USWGTreSubsystem* TreSubsystem);
	/** Game thread: turns a finished globe into textures and material parameters. */
	void ApplyRegion(const FSWGPlanetGlobe& Globe, const FSWGTerrainBakeResult& Baked, const FLinearColor& DetailAverage);
	void CancelRegionBake();

	FRegionState Region;
	UPROPERTY()
	TObjectPtr<UTexture2D> RegionColourTexture;
	UPROPERTY()
	TObjectPtr<UTexture2D> RegionNormalTexture;
	UPROPERTY()
	TObjectPtr<UTexture2D> RegionFarTexture;
	UPROPERTY()
	TObjectPtr<UTexture2D> RegionDetailTexture;
	/** Keeps the retail sprite materials alive after the star material replaces them (Celestials is not a reflected property). */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> RetainedRetailMaterials;
	TWeakObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorRef;
	int32 MainSunIndex = INDEX_NONE;
	FLinearColor SunStarColour = FLinearColor::White;
	TFuture<void> RegionTask;
	TSharedPtr<FThreadSafeBool> RegionCancel;

	FVector SunDirection = FVector::UpVector;
	FLinearColor SunRadiance = FLinearColor::White;
	bool bHasSun = false;
	TArray<TWeakObjectPtr<ADirectionalLight>> SourceLights;
	UPROPERTY()
	TArray<TObjectPtr<UDirectionalLightComponent>> PlanetLights;
};
