#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGTextureRenderer.h"
#include "Common/SWGTerrainBake.h"

/** Layout of a planet's globe maps; every field except the seed is part of the cache key. */
struct SWGEMUCLIENT_API FSWGPlanetGlobeParams
{
	/** Angular radius, radians, of the cap of the surface the viewer sees: acos(surface radius / distance). */
	float CapRadius = .4f;
	/** The playable map's half width as a fraction of CapRadius; 1 or more covers the whole visible cap with real terrain. */
	float Fill = 1.f;
	/** Pixels along each edge of the cap map. */
	int32 CapResolution = 1024;
	/** Varies the generated extension per planet. */
	uint32 Seed = 0;

	/** The cap map spans this multiple of CapRadius each way, so the visible cap sits inside its fade. */
	static constexpr float CapMargin = 1.25f;

	float GetCapHalfWidth() const { return FMath::Min(CapRadius*CapMargin, 1.5f); }
	/** The whole-globe map is twice as wide as tall: the cap's own 2:1 up to 4096 x 2048, where it stops (it only shows beyond the cap). */
	int32 GetFarWidth() const { return FMath::Min(CapResolution*2, 4096); }
	int32 GetFarHeight() const { return GetFarWidth()/2; }
	/** The cap's detail-weight map is half the cap map's resolution (see FSWGTerrainBake::GetDetailResolution). */
	int32 GetCapDetailResolution() const { return FMath::Max(CapResolution/2, 64); }
};

/**
 * One planet's ground terrain as globe textures. Both maps are equirectangular around the cap centre: u runs east, v runs south,
 * (longitude, latitude) are measured from the cap centre with north toward the planet's pole.
 */
struct SWGEMUCLIENT_API FSWGPlanetGlobe
{
	/** Cap map: longitude and latitude within +-GetCapHalfWidth(), sRGB, alpha = water classification. */
	FSWGImage CapColour;
	/** Cap relief: R = east, G = north, B = up (linear, 128 = 0), alpha = how real the terrain is there (255 = the retail terrain itself, 0 = generated). */
	FSWGImage CapNormal;
	/** Whole globe, longitude +-PI by latitude +-PI/2, sRGB, alpha = water. */
	FSWGImage FarColour;
	/** Cap detail weights: the cap map's extent at GetCapDetailResolution(), R..A the blend weights of DetailFamilies[0..3] (linear). */
	FSWGImage CapDetail;
	/** The ground bake's detail families (the weights' meaning). */
	TArray<FSWGTerrainDetailFamily> DetailFamilies;
	FSWGPlanetGlobeParams Params;
	/** Seconds spent composing the maps (a cache hit reports the original build). */
	double BuildSeconds = 0;
	/** Seconds this load took, including cache reads. */
	double LoadSeconds = 0;
	bool bFromCache = false;
	FString CacheBasePath;
};

/** Composes a planet globe from a baked ground zone: the real terrain where it exists, a mirrored and warped copy of it elsewhere. Touches no UObjects. */
class SWGEMUCLIENT_API FSWGPlanetGlobeBake
{
public:
	/** Real map half width as a fraction of its own stored half width that is trusted; the rim ramps and is mirrored away. */
	static constexpr float UsableFraction = .96f;

	/** Cached build, or a fresh one that is then cached (when Real carries a cache path). False if Real has no pixels. */
	static bool LoadOrBuild(const FSWGTerrainBakeResult& Real, const FSWGPlanetGlobeParams& Params, FSWGPlanetGlobe& OutGlobe);

	/** <real cache base>_globe<cap>_c<cap radius in 0.1 degrees>_f<fill in percent>_d<detail version>, no extension. */
	static FString MakeCacheBasePath(const FString& RealCacheBasePath, const FSWGPlanetGlobeParams& Params);

	/** Raw ground units per radian of arc: the real map's half width spans Fill * CapRadius. */
	static float GetGroundPerRadian(float MapSize, const FSWGPlanetGlobeParams& Params);

	/** Cap map UV (v down) of the direction Theta radians from the cap centre toward the compass bearing Azimuth (0 = north, PI/2 = east). */
	static FVector2D GetCapUv(const FSWGPlanetGlobeParams& Params, float Theta, float Azimuth);

	/** Whole-globe map UV (v down) of a direction by longitude and latitude. */
	static FVector2D GetFarUv(float Longitude, float Latitude);

	/** Nearest texel of an image at a UV (v down). */
	static FColor SampleNearest(const FSWGImage& Image, const FVector2D& Uv);

private:
	static void Build(const FSWGTerrainBakeResult& Real, const FSWGPlanetGlobeParams& Params, FSWGPlanetGlobe& OutGlobe);
	static bool WriteCache(const FString& CacheBasePath, const FSWGPlanetGlobe& Globe);
	static bool ReadCache(const FString& CacheBasePath, const FSWGPlanetGlobeParams& Params, FSWGPlanetGlobe& OutGlobe);
};
