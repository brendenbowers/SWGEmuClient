#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGTextureRenderer.h"

class USWGTreSubsystem;
struct FSWGTerrainData;

/** A terrain shader family that gets a fine-scale detail texture on the planet globe. */
struct SWGEMUCLIENT_API FSWGTerrainDetailFamily
{
	int32 FamilyId = 0;
	/** The diffuse .dds of the family's first layer. */
	FString DiffusePath;
	/** Linear mean of that texture; the detail is applied as texture / Average, so it adds variation and not brightness. */
	FLinearColor Average = FLinearColor::Gray;
};

/** A ground zone's playable extent as square images, north up and east right (what an overhead map shows). */
struct SWGEMUCLIENT_API FSWGTerrainBakeResult
{
	/** Retail colour map times the dominant shader family's average texture colour (sRGB); alpha is the water classification (255 under a water table, else 0). */
	FSWGImage Colour;
	/** Tangent-space relief: R = east, G = north, B = up, linear, 128 = 0. */
	FSWGImage Normal;
	/** The land's dominant shader families, strongest first (at most DetailFamilyCount); empty for a fallback map. */
	TArray<FSWGTerrainDetailFamily> DetailFamilies;
	/** Blend weight of DetailFamilies[0..3] in R..A (255 = the family alone; 0 under water), GetDetailResolution() square, linear. Empty for a fallback map. */
	FSWGImage DetailWeights;
	/** Heights scaled to 0..65535 between MinHeight and MaxHeight; empty for a fallback map. */
	TArray<uint16> Heights;
	float MinHeight = 0;
	float MaxHeight = 0;
	/** Raw units along each edge. */
	float MapSize = 0;
	/** Mean of Colour, linear. */
	FLinearColor AverageColour = FLinearColor::Gray;
	/** Wall time of the bake that produced this data (a cache hit reports the original bake). */
	double BakeSeconds = 0;
	/** Wall time of this load, including cache reads. */
	double LoadSeconds = 0;
	bool bFromCache = false;
	/** The retail overhead map stands in for a bake. */
	bool bFallbackMap = false;
	FString CacheBasePath;
};

/** Bakes a ground .trn off the game thread; touches no UObjects. Shared by the space planets (the holo map keeps its own baker for now). */
class SWGEMUCLIENT_API FSWGTerrainBake
{
public:
	static constexpr int32 DefaultResolution = 1024;
	/** The top quality tier bakes this: 4096 over the 16384-unit map is 4 m a pixel. */
	static constexpr int32 MaxResolution = 4096;
	/** Detail families blended per planet (the four channels of the weight map). */
	static constexpr int32 DetailFamilyCount = 4;
	/** Part of the cache key; bump when the detail families or weights change meaning. */
	static constexpr int32 DetailVersion = 1;
	/** The weight map is half the ground bake's resolution: the weights vary slowly and the detail textures carry the fine scale. */
	static int32 GetDetailResolution(int32 Resolution) { return FMath::Max(Resolution/2, 64); }
	/** Relief is baked this much steeper than the real slope so the shading reads at planet scale. */
	static constexpr float ReliefExaggeration = 1.5f;

	/** terrain/<name>.trn for a STAT planet appearance (appearance/planet_<name>.pln) when that file exists; empty for moons and others. */
	static FString FindGroundTerrain(const USWGTreSubsystem& Tre, const FString& PlanetAppearance);

	/** Cached bake, or a fresh one that is then cached. False if the terrain cannot be read. */
	static bool LoadOrBake(const USWGTreSubsystem& Tre, const FString& TerrainPath, int32 Resolution, const FString& CacheDirectory,
		FSWGTerrainBakeResult& OutResult);

	/** texture/ui_map_<name>.dds as a colour map with flat relief. */
	static bool LoadFallbackMap(const USWGTreSubsystem& Tre, const FString& TerrainPath, FSWGTerrainBakeResult& OutResult);

	/** Linear mean of a texture (.dds path); cached per path. False if it cannot be decoded. */
	static bool GetAverageColour(const USWGTreSubsystem& Tre, const FString& TexturePath, FLinearColor& OutColour);

	/** The diffuse .dds a terrain shader family layer (or a .sht path) names, as GetOrLoadShaderTexture resolves it. */
	static FString ResolveDiffusePath(const USWGTreSubsystem& Tre, const FString& LayerOrShader);

	/** <directory>/<name>_<hash of the terrain bytes>_<resolution>_d<detail version>, no extension. */
	static FString MakeCacheBasePath(const FString& CacheDirectory, const FString& TerrainPath, const TArray<uint8>& TerrainBytes, int32 Resolution);

	/** Reads the .png and .bin written by a bake; false if either is missing or from another cache version. */
	static bool ReadCache(const FString& CacheBasePath, FSWGTerrainBakeResult& OutResult);

	/** Parses the .trn bytes and loads the colour-ramp images its affectors name. */
	static bool ParseGround(const USWGTreSubsystem& Tre, TArray<uint8> TerrainBytes, FSWGTerrainData& OutData);

	/** Square bake centred on the origin; WindowSize 0 means the terrain's own MapSize, larger samples beyond the playable extent. */
	static void BakeData(const USWGTreSubsystem& Tre, const FSWGTerrainData& Data, int32 Resolution, FSWGTerrainBakeResult& OutResult, float WindowSize = 0);

private:
	static bool WriteCache(const FString& CacheBasePath, const FSWGTerrainBakeResult& Result);
};
