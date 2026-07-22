#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TRE/SWGTerrainReader.h"
#include "SWGTerrainSubsystem.generated.h"

class USWGTreSubsystem;
class USWGMeshGeneratorSubsystem;
class ALandscape;
class UDataTable;
class UTexture2D;
class UMaterialInterface;

/**
 * One static world-snapshot object (building, wall, pillar, item, etc.)
 * resolved and ready to spawn — see USWGTerrainSubsystem::LoadWorldSnapshotObjects.
 */
struct FSWGWorldSnapshotSpawnInfo
{
	TSubclassOf<AActor> ActorClass;
	FVector Position = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	FString TemplateName;
};

/** Result of BakeHeightmap (or a cache hit) — everything SpawnLandscape needs. */
struct FSWGBakedHeightmap
{
	/** Row-major, HeightmapResolution x HeightmapResolution. Raw float heights — Landscape's uint16 encoding happens in SpawnLandscape. */
	TArray<float> Heights;

	/** World-space (X,Y) of Heights[0] — the grid's min corner, not the center. */
	FVector Origin = FVector::ZeroVector;

	/** World units between adjacent heightmap samples. */
	float Spacing = 0.0f;

	/**
	 * Up to 4 shader-family IDs this tile blends between (see BakeShaderWeights),
	 * ordered by total paint weight across the tile — [0] is the dominant
	 * ("base") family and has no corresponding vertex-color channel (its weight
	 * is implicit: 1 - R - G - B). Empty if this tile's terrain data had no
	 * shader affectors evaluate to anything (falls back to the plain default
	 * material, same as before real texturing existed).
	 */
	TArray<int32> ChosenShaderFamilyIds;

	/** Row-major, same layout as Heights — VertexColors[i].X/Y/Z are family [1]/[2]/[3]'s paint weight at that vertex. */
	TArray<FVector3f> ShaderWeightColors;
};

/**
 * Orchestrates the terrain pipeline end to end: cache check, .trn parse
 * (FSWGTerrainReader), heightmap baking for the region around the player's
 * spawn point, and ALandscape population — see world-object-plan.html
 * "Message -> visible-in-level" (ti1-ti7) for the full design. Triggered from
 * FSWGZoneLoadingState::Enter() using CmdStartScene's TerrainName + spawn
 * position, both already available via FSWGSceneStartPayload.
 *
 * Empty skeleton for now — every step below is a stub. Filling these in is
 * separate follow-up work per the phased plan.
 */
UCLASS()
class SWGEMUCLIENT_API USWGTerrainSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Entry point (ti2): kicks off cache check -> parse -> bake -> spawn for the
	 * region around SpawnPosition. Broadcasts OnTerrainReady when the landscape
	 * for that region is spawned and collidable.
	 */
	void BeginLoadTerrain(const FString TerrainVirtualPath, const FVector& SpawnPosition);

	/** Broadcast once BeginLoadTerrain's region has a spawned, collidable landscape. */
	DECLARE_MULTICAST_DELEGATE(FOnTerrainReady);
	FOnTerrainReady OnTerrainReady;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnTerrainError, const FString& /*ErrorMessage*/);
	FOnTerrainError OnTerrainError;

	/**
	 * Diagnostic: evaluates the same height function the baked heightmap uses,
	 * for comparing against a live actor's network-received Z. Returns 0 if
	 * no terrain has parsed yet. Takes and returns RAW/native-space
	 * coordinates (matching the .trn file's own units and the network wire's
	 * raw X/Y/Z, NOT final UE-space actor positions) — see Common/SWGWorldScale.h.
	 * Callers comparing against a network position should pass that position's
	 * own raw X/Y unconverted; comparing against a live actor's UE Location
	 * needs SWGToRawSpace() first.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Terrain")
	float GetHeightAt(float X, float Y) const;

private:
	void Error(const FString& ErrorMessage);

	void LoadTerrain(const FString& TerrainVirtualPath, const FVector& SpawnPosition);

	/** Creates the outdoor sky, sun, and ambient fill for the active planet. */
	void SetupPlanetLighting(const FString& TerrainVirtualPath);

	/** ti3: check Saved/TerrainCache/ for this (TerrainVirtualPath, region) before parsing/baking. RegionOrigin is a component's min corner (see BakeHeightmap). */
	bool FindCachedHeightmap(const FString& TerrainVirtualPath, const FVector& RegionOrigin, FSWGBakedHeightmap& OutHeightmap);

	/** ti4: USWGTreSubsystem::CreateIffReader + FSWGTerrainReader::ReadTerrain — synchronous, cheap. */
	bool ParseTerrain(const FString& TerrainVirtualPath, FSWGTerrainData& OutTerrainData);

	/**
	 * ti5: evaluate FSWGTerrainEvaluator::GetHeight(x,y) across one component's
	 * region (RegionOrigin = min corner, HeightmapResolution x HeightmapResolution
	 * samples spaced HeightmapWorldExtent/(HeightmapResolution-1) apart), on a
	 * background thread (this is called from). Independent components sharing a
	 * RegionOrigin exactly HeightmapWorldExtent apart get identical heights at
	 * their shared edge, since GetHeight is a deterministic pure function of world
	 * (x,y) — no separate seam-stitching needed for grid tiling.
	 */
	FSWGBakedHeightmap BakeHeightmap(const FSWGTerrainData& TerrainData, const FVector& RegionOrigin);

	/**
	 * Companion bake, same region/resolution as BakeHeightmap: evaluates
	 * FSWGTerrainEvaluator::GetShaderWeights at every sample, picks this
	 * tile's top (up to) 4 shader families by total paint weight, and packs
	 * per-vertex weights into Heightmap.ShaderWeightColors — see
	 * FSWGBakedHeightmap's own comment for the exact channel layout.
	 */
	void BakeShaderWeights(const FSWGTerrainData& TerrainData, FSWGBakedHeightmap& Heightmap);

	/**
	 * Builds (or returns an already-built) UMaterialInstanceDynamic for this
	 * tile's ChosenShaderFamilyIds, loading each family's primary diffuse
	 * texture.dds via TreSubsystem + FSWGDDSTextureLoader (memoized in
	 * LoadedShaderTextures — adjacent tiles very commonly share their
	 * dominant family, no reason to decode the same .dds twice). Falls back
	 * to the plain default material if Heightmap has no chosen families.
	 */
	UMaterialInterface* BuildTerrainTileMaterial(const FSWGBakedHeightmap& Heightmap);

	/** Resolves shader/<FamilyLayerName>.sht and loads its tagged texture slot. */
	UTexture2D* GetOrLoadShaderTexture(const FString& LayerName, bool bNormalMap = false);

	/** ti6: spawn one ALandscape actor at the whole grid's min corner (game thread). */
	ALandscape* SpawnLandscapeActor(const FVector& GridOrigin, float Spacing);

	/** ti6: build/register one component from a baked heightmap at the given SectionBase (quad units, game thread). */
	void AddLandscapeComponent(ALandscape* Landscape, const FSWGBakedHeightmap& Heightmap, const FIntPoint& SectionBase);

	/** ti6: spawn the actor + every component in the grid (game thread). Kept
	 *  around but no longer called (see SpawnDynamicMeshTerrainGrid) — its
	 *  ULandscapeComponent scale composition is undocumented/unreliable, and a
	 *  plain mesh sidesteps that by working in direct world-space units. */
	void SpawnLandscapeGrid(const TArray<FSWGBakedHeightmap>& Grid, const FVector& GridOrigin, float Spacing);

	/**
	 * Active terrain rendering path: one UDynamicMeshComponent per baked grid
	 * tile, vertices placed directly in world-space units (no actor/component
	 * scale beyond identity) — same approach USWGMeshGeneratorSubsystem already
	 * uses for buildings/props, chosen specifically to avoid ULandscapeComponent's
	 * scale-encoding indirection that caused the "terrain ~1000 units below the
	 * buildings" bug.
	 */
	void SpawnDynamicMeshTerrainGrid(const TArray<FSWGBakedHeightmap>& Grid, const FVector& GridOrigin, float Spacing);

	/**
	 * Parses snapshot/<zone>.ws (the client-side counterpart to Core3's own
	 * loadSnapshotObjects — static world content like buildings/walls that's
	 * never sent over the network), keeps only
	 * top-level nodes within WorldSnapshotSpawnRadius of SpawnPosition, and
	 * resolves each one's actor class the same way SceneCreateObjectByCrc
	 * dispatch does (root FORM tag -> DT_SWGFormTagMappings), just keyed by
	 * template path instead of CRC. Safe to call off the game thread (no
	 * UObject spawning here, just parsing/resolving) — mirrors ParseTerrain/
	 * BakeHeightmap's own thread-safety story.
	 */
	TArray<FSWGWorldSnapshotSpawnInfo> LoadWorldSnapshotObjects(const FString& TerrainVirtualPath, const FVector& SpawnPosition);

	/** Spawns every resolved object from LoadWorldSnapshotObjects (game thread only). */
	void SpawnWorldSnapshotObjects(const TArray<FSWGWorldSnapshotSpawnInfo>& Objects);

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> TreSubsystem;

	UPROPERTY()
	TObjectPtr<USWGMeshGeneratorSubsystem> MeshGenerator;

	/** Lazily loaded on first use — see LoadWorldSnapshotObjects. Same DataTable SWGInitializationState uses for CRC dispatch. */
	UPROPERTY()
	TObjectPtr<UDataTable> FormTagMappingTable;

	/** Parent material for BuildTerrainTileMaterial's per-tile MIDs — a simple vertex-color blend of up to 4 texture parameters (Layer0..Layer3). Authored as a plugin content asset, not generated at runtime. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TerrainBlendMaterial;

	/** TRE virtual path's texture (e.g. "rock_cliff_anza" -> texture/rock_cliff_anza.dds) -> decoded transient UTexture2D. See GetOrLoadShaderTexture. */
	UPROPERTY()
	TMap<FString, TObjectPtr<UTexture2D>> LoadedShaderTextures;

	// How far from the spawn position to keep world-snapshot objects (9099
	// total nodes for all of tatooine — spawning every one would be wasteful
	// and mostly irrelevant to where the player actually is). Cut down
	// alongside ComponentGridSize (see its own comment) now that mesh
	// geometry is back to human-scale UE units — the old 3000 (matching a
	// ~3-tile-wide baked area) would now try to spawn props across a
	// footprint we no longer bake terrain for at all.
	static constexpr float WorldSnapshotSpawnRadius = 1000.0f;

	// Cached from the last successful ParseTerrain in LoadTerrain, so GetHeightAt
	// can re-evaluate the same height function on demand (diagnostics, and any
	// future on-the-fly-height need) without re-parsing the .trn each call.
	FSWGTerrainData CachedTerrainData;
	bool bTerrainDataCached = false;

	// Bake grid tuning — placeholder extent, not a finished sizing decision (see
	// world-object-plan.html "Heightmap baking + local cache": "resolution TBD
	// against Landscape's expected sizing"). Resolution IS constrained though:
	// ULandscapeComponent requires SubsectionSizeQuads+1 to be a power of two
	// (LandscapeComponent.h:449) — 128 samples = 127 quads = a single valid
	// subsection (SubsectionSizeQuads=127, NumSubsections=1).
	static constexpr int32 HeightmapResolution = 128; // samples per axis

	// 12700 (the original placeholder) looked "way too large" in-game — confirmed
	// against real terrain data why: decoded BoundaryRectangle/BoundaryCircle sizes
	// from tatooine.trn show local content features (flora patches, shader-blend
	// zones) mostly sized 600-4600 units across, with only the large *regional*
	// shaping rectangles reaching ~17000 — i.e. 12700 was sized like a whole region,
	// not a single local feature. 2032 (=127*16, a clean 16 units/quad spacing)
	// matches one typical local feature's scale instead.
	static constexpr float HeightmapWorldExtent = 2032.0f; // world units per axis, centered on the spawn position

	// Tile ComponentGridSize x ComponentGridSize components under one ALandscape
	// actor, centered on the spawn point. 1 (not 3x3) since that's already more
	// world than the player can meaningfully see and far cheaper to bake.
	static constexpr int32 ComponentGridSize = 1;
};
