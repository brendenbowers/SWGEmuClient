#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TRE/SWGTerrainReader.h"
#include "TRE/SWGWorldSnapshotReader.h"
#include "SWGTerrainSubsystem.generated.h"

class USWGTreSubsystem;
class USWGMeshGeneratorSubsystem;
class USWGObjectGraphSubsystem;
class ALandscape;
class UDataTable;
class UTexture2D;
class UMaterialInterface;
class UDynamicMeshComponent;

namespace UE::Geometry { class FDynamicMesh3; }

/**
 * One static world-snapshot object (building, wall, pillar, item, etc.)
 * resolved and ready to spawn — see USWGTerrainSubsystem::ResolveSnapshotObjectsForTile.
 * Mirrors the .ws node tree: a building's Children are its cells, a cell's
 * Children are the props placed inside it.
 */
struct FSWGWorldSnapshotSpawnInfo
{
	/** The .ws ObjectID. Core3 creates its client objects under this same id,
	 *  so containment/targeting messages reference it — see
	 *  USWGObjectGraphSubsystem::RegisterStaticObject. */
	int64 ObjectId = 0;

	/** The .ws CellID — only meaningful when ActorClass is ASWGCell. */
	int32 CellNumber = 0;

	TSubclassOf<AActor> ActorClass;

	/** Raw space. World for a top-level node, parent-relative for a child (same convention as SceneCreateObjectByCrc for contained objects). */
	FVector Position = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	FString TemplateName;

	TArray<FSWGWorldSnapshotSpawnInfo> Children;
};

/** Result of BakeHeightmap — everything one tile's mesh needs. */
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
 * One tile, baked and triangulated on a worker, ready for the game thread to
 * move straight into a component. Triangulating there instead cost 250-375 ms a
 * tile against the bake's ~13 ms, which is what capped terrain density.
 */
struct FSWGTerrainTileBuild
{
	FSWGBakedHeightmap Heightmap;

	/** Shared only so the async plumbing can copy the handle; nothing shares the mesh itself. */
	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> Mesh;
};

/**
 * Ground the terrain mesh is not generated over, so a room whose floor sits
 * below the surrounding terrain isn't intruded on. Oriented, since rooms are
 * rectangular in the building's frame and buildings carry a yaw. Raw space.
 */
struct FSWGTerrainHole
{
	FVector2D Center = FVector2D::ZeroVector;

	/** Half-size along the hole's own local axes, before yaw. */
	FVector2D Extents = FVector2D::ZeroVector;

	float YawRadians = 0.0f;

	/** The building that cut it, so its holes leave with it. 0 = anonymous, kept for the zone. */
	int64 OwnerObjectId = 0;

	bool Contains(const FVector2D& Point) const;

	/** Axis-aligned world bounds of the rotated rectangle. */
	FBox2D GetWorldBounds() const;
};

/**
 * Streams the terrain around the local player as a grid of 512 m tiles, the
 * way retail's client generated chunks around its camera (there is no
 * authored partition in the .trn — see FSWGTerrainHeader). Each tile is one
 * UDynamicMeshComponent baked on a worker from the immutable planet data plus
 * an overlay of runtime edits (building pads, room holes); the .ws objects
 * rooted in a tile spawn inside a tighter ring (the server only sends
 * network objects within ~192 m, so distant statics are scenery) and are
 * destroyed again when the player leaves it.
 *
 * Triggered from FSWGZoneLoadingState::Enter() via BeginLoadTerrain with
 * CmdStartScene's TerrainName + spawn position. OnTerrainReady fires once the
 * tiles immediately around the spawn are collidable; the rest of the ring
 * streams in behind it.
 */
UCLASS()
class SWGEMUCLIENT_API USWGTerrainSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bTerrainDataCached; }

	/**
	 * Entry point: parses the .trn on a worker, then streams tiles around
	 * SpawnPosition (and the player, once one exists). Broadcasts
	 * OnTerrainReady when the tiles around the spawn are spawned and collidable.
	 */
	void BeginLoadTerrain(const FString TerrainVirtualPath, const FVector& SpawnPosition);

	/** Broadcast once BeginLoadTerrain's spawn-area tiles have a spawned, collidable mesh. */
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

	/**
	 * Stamps a runtime-placed object's terrain modification into the live
	 * terrain and re-bakes only the loaded tiles it overlaps; tiles loaded
	 * later bake with it already applied.
	 *
	 * TemplatePath is the object's shared template (.iff); its
	 * "terrainModificationFileName" (.lay layer graph) is what gets stamped.
	 * Objects without one are a no-op.
	 *
	 * WorldPosition/YawRadians are RAW/native space (the .trn's own units and
	 * the network wire's raw X/Y/Z), matching GetHeightAt — not final UE-space
	 * actor coordinates.
	 *
	 * OwnerObjectId ties the edit to its object so RemoveObjectTerrainEdits can
	 * take it back out when a streamed .ws building unloads; a second call for
	 * the same owner is ignored rather than stacked.
	 *
	 * Game thread only. Safe before the terrain loads — the edit is held and
	 * applied once it has.
	 *
	 * Returns false only when the template has no terrain modification at all.
	 */
	bool ApplyObjectTerrainModification(const FString& TemplatePath, const FVector& WorldPosition, float YawRadians, int64 OwnerObjectId = 0);

	/**
	 * Registers areas the terrain must not cover — see FSWGTerrainHole. Safe
	 * before the terrain loads. Game thread only. Raw/native space.
	 */
	void AddTerrainHoles(const TArray<FSWGTerrainHole>& Holes);

	/** Drops every pad layer and hole registered under OwnerObjectId and re-bakes the loaded tiles they covered. Game thread. */
	void RemoveObjectTerrainEdits(int64 OwnerObjectId);

	/**
	 * Spawns one node at WorldTransform (UE space), registers it with the object
	 * graph under its .ws id, and recurses into its children. Parent is the
	 * already-spawned owner (a building for a cell, a cell for a prop). A closed
	 * room is deferred onto its building for USWGInteriorStreamingSubsystem
	 * unless bForceInterior — ASWGBuilding::LoadRoom's re-entry. Every actor
	 * created, at any depth, is appended to OutSpawned when given.
	 */
	AActor* SpawnWorldSnapshotNode(const FSWGWorldSnapshotSpawnInfo& Info, const FTransform& WorldTransform, AActor* Parent, USWGObjectGraphSubsystem* ObjectGraph, bool bForceInterior = false, TArray<TWeakObjectPtr<AActor>>* OutSpawned = nullptr);

private:
	/**
	 * Everything a bake reads, captured by value per job so workers never look
	 * at subsystem state. The planet data is immutable and shared; the edit
	 * overlay is replaced wholesale (copy-on-write) whenever an edit lands,
	 * so a job in flight keeps the version it started with.
	 */
	struct FSWGTerrainBakeSource
	{
		TSharedPtr<const FSWGTerrainData, ESPMode::ThreadSafe> Planet;
		TSharedPtr<const TArray<FSWGTerrainLayer>, ESPMode::ThreadSafe> EditLayers;
		TArray<FSWGTerrainHole> Holes;
		int32 EditVersion = 0;
	};

	/** A pad layer with the object that placed it, so RemoveObjectTerrainEdits can find it. */
	struct FSWGOwnedTerrainLayer
	{
		int64 OwnerObjectId = 0;
		FSWGTerrainLayer Layer;
	};

	/**
	 * A terrain modification requested before the terrain existed. Buildings
	 * routinely spawn ahead of the async .trn parse finishing, so these are
	 * held and replayed rather than dropped — dropping them meant a
	 * building's pad silently never applied.
	 */
	struct FSWGPendingTerrainModification
	{
		FString TemplatePath;
		FVector WorldPosition = FVector::ZeroVector;
		float YawRadians = 0.0f;
		int64 OwnerObjectId = 0;
	};

	/** One streamed tile's game-thread state. Keyed in Tiles by its grid coordinate. */
	struct FSWGTerrainTile
	{
		/** Null until the first bake lands. Pooled on unload. */
		TObjectPtr<UDynamicMeshComponent> Component;

		FSWGBakedHeightmap Heightmap;

		/** The edit version this tile should show; bumped by any edit overlapping it. */
		int32 WantedEditVersion = 0;

		/** The edit version the live mesh was baked with; -1 until one lands. */
		int32 LiveEditVersion = -1;

		bool bBakeInFlight = false;

		/** Every actor this tile's .ws objects produced, at any depth, destroyed with the tile. */
		TArray<TWeakObjectPtr<AActor>> SnapshotActors;

		/** Resolved .ws objects not yet spawned — drained a few per frame by SpawnPendingSnapshotObjects. */
		TArray<FSWGWorldSnapshotSpawnInfo> PendingSnapshotObjects;
		int32 NextSnapshotIndex = 0;

		/** Set once the tile's objects have been queued (or it has none); cleared when they are destroyed. */
		bool bSnapshotSpawned = false;
		bool bSnapshotResolveInFlight = false;
	};

	// ── Load ─────────────────────────────────────────────────────────────

	void Error(const FString& ErrorMessage);

	/** Worker: parses the .trn and .ws, then hands both to the game thread to start streaming. */
	void LoadTerrain(const FString& TerrainVirtualPath, const FVector& SpawnPosition);

	/** Creates the outdoor sky, sun, and ambient fill for the active planet. */
	void SetupPlanetLighting(const FString& TerrainVirtualPath);

	/** USWGTreSubsystem::CreateIffReader + FSWGTerrainReader::ReadTerrain — synchronous, cheap. */
	bool ParseTerrain(const FString& TerrainVirtualPath, FSWGTerrainData& OutTerrainData);

	/** Spawns the empty root actor every tile component attaches to. Game thread. */
	void SpawnTerrainActor();

	/** Tears down every tile, snapshot actor and queued job of the current zone. Game thread. */
	void ResetZone();

	// ── Streaming ────────────────────────────────────────────────────────

	/** Grid coordinate of the tile containing a raw-space position. */
	static FIntPoint TileCoordAt(const FVector2D& RawPosition);

	/** Raw-space min corner of a tile. */
	static FVector TileOrigin(const FIntPoint& Coord);

	/** Raw-space XY extent of a tile. */
	static FBox2D TileBounds(const FIntPoint& Coord);

	/** Whether a tile lies (at least partly) inside the planet's playable extent. */
	bool IsTileOnMap(const FIntPoint& Coord) const;

	/**
	 * Re-evaluates the wanted tile set around the streaming centre: queues
	 * bakes for missing tiles (nearest first, biased one tile along the
	 * player's heading), unloads tiles past UnloadRadiusTiles, then pumps the
	 * bake queue. Game thread; runs every StreamingSweepInterval.
	 */
	void UpdateStreaming();

	/** The raw-space position streaming is centred on — the pawn once it exists, the spawn point before that. */
	bool GetStreamingCenter(FVector2D& OutRawPosition) const;

	/** Starts worker bakes for queued tiles, nearest first, up to MaxBakesInFlight. Game thread. */
	void PumpBakeQueue();

	/** Kicks one tile's bake on a worker. Game thread. */
	void StartTileBake(const FIntPoint& Coord);

	/** A finished bake arriving on the game thread — applied, or dropped if the tile is gone or stale. */
	void OnTileBakeFinished(const FIntPoint& Coord, int32 Generation, int32 EditVersion, FSWGTerrainTileBuild& Build);

	/** Destroys a tile's snapshot actors and pools its component. Game thread. */
	void UnloadTile(const FIntPoint& Coord);

	/** Fresh or pooled, attached to the terrain actor at the tile's origin. Game thread. */
	UDynamicMeshComponent* AcquireTileComponent(const FIntPoint& Coord);

	/**
	 * Bakes and triangulates one tile end to end. Worker thread — it touches no
	 * UObject; everything it reads is in Source. See FSWGTerrainTileBuild.
	 */
	FSWGTerrainTileBuild BakeTerrainTile(const FSWGTerrainBakeSource& Source, const FIntPoint& Coord) const;

	/** Hands a finished build to its component — a mesh move, a material, and an async collision request. Game thread. */
	void ApplyTerrainTileBuild(const FIntPoint& Coord, FSWGTerrainTile& Tile, FSWGTerrainTileBuild& Build);

	/** Snapshot of the current planet + edits for a job to carry. Game thread. */
	FSWGTerrainBakeSource MakeBakeSource() const;

	// ── Edits ────────────────────────────────────────────────────────────

	/** Replays everything queued while the terrain was still loading. Game thread, called once the planet data exists. */
	void FlushPendingObjectTerrainModifications();

	/** Resolves TemplatePath's .lay into world-space layers ready for the edit overlay. Off-thread safe. */
	bool BuildObjectTerrainLayers(const FString& TemplatePath, const FVector& WorldPosition, float YawRadians, TArray<FSWGTerrainLayer>& OutLayers);

	/** Rebuilds the shared edit overlay from OwnedEditLayers and bumps EditVersion. Game thread. */
	void PublishEditLayers();

	/** Marks every loaded tile overlapping Bounds as wanting the current edit version and queues its re-bake. Game thread. */
	void InvalidateTilesOverlapping(const FBox2D& Bounds);

	/** World-space XY extent one baked tile covers. */
	static FBox2D GetTileBounds(const FSWGBakedHeightmap& Heightmap);

	// ── Rendering ────────────────────────────────────────────────────────

	/**
	 * Evaluate FSWGTerrainEvaluator::GetHeight(x,y) across one tile's region
	 * (RegionOrigin = min corner, HeightmapResolution x HeightmapResolution
	 * samples spaced HeightmapWorldExtent/(HeightmapResolution-1) apart), on a
	 * background thread. Adjacent tiles get identical heights at their shared
	 * edge, since GetHeight is a deterministic pure function of world (x,y) —
	 * no separate seam-stitching needed.
	 */
	FSWGBakedHeightmap BakeHeightmap(const FSWGTerrainBakeSource& Source, const FVector& RegionOrigin) const;

	/**
	 * Companion bake, same region/resolution as BakeHeightmap: evaluates
	 * FSWGTerrainEvaluator::GetShaderWeights at every sample, picks this
	 * tile's top (up to) 4 shader families by total paint weight, and packs
	 * per-vertex weights into Heightmap.ShaderWeightColors — see
	 * FSWGBakedHeightmap's own comment for the exact channel layout.
	 */
	void BakeShaderWeights(const FSWGTerrainBakeSource& Source, FSWGBakedHeightmap& Heightmap) const;

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

	/** Landscape path, kept but unused: spawn one ALandscape actor at the whole grid's min corner (game thread). */
	ALandscape* SpawnLandscapeActor(const FVector& GridOrigin, float Spacing);

	/** Landscape path, kept but unused: build/register one component from a baked heightmap at the given SectionBase (quad units, game thread). */
	void AddLandscapeComponent(ALandscape* Landscape, const FSWGBakedHeightmap& Heightmap, const FIntPoint& SectionBase);

	/** Landscape path, kept but unused — its ULandscapeComponent scale composition is undocumented/unreliable, and a plain mesh sidesteps that. */
	void SpawnLandscapeGrid(const TArray<FSWGBakedHeightmap>& Grid, const FVector& GridOrigin, float Spacing);

	// ── World snapshot ───────────────────────────────────────────────────

	/**
	 * Parses snapshot/<zone>.ws (the client-side counterpart to Core3's own
	 * loadSnapshotObjects — static world content like buildings/walls that's
	 * never sent over the network) and buckets its top-level nodes by tile
	 * coordinate. Worker thread; the result is read-only from then on.
	 */
	TSharedPtr<const FSWGWorldSnapshotData, ESPMode::ThreadSafe> LoadWorldSnapshot(const FString& TerrainVirtualPath, TMap<FIntPoint, TArray<int32>>& OutNodesByTile);

	/**
	 * Resolves the .ws nodes rooted in one tile the same way SceneCreateObjectByCrc
	 * dispatch does (root FORM tag -> DT_SWGFormTagMappings), just keyed by
	 * template path instead of CRC. Off the game thread, inside the tile's bake.
	 */
	TArray<FSWGWorldSnapshotSpawnInfo> ResolveSnapshotObjectsForTile(const FIntPoint& Coord) const;

	/** Resolves one .ws node (and its subtree) into OutInfo. False if the template resolves to no actor class. */
	bool ResolveWorldSnapshotNode(const FSWGWorldSnapshotNode& Node, const FSWGWorldSnapshotData& SnapshotData, FSWGWorldSnapshotSpawnInfo& OutInfo) const;

	/**
	 * Resolves a tile's .ws objects on a worker and queues them for spawning
	 * when it lands (unless the player has since moved out of range). Static
	 * objects use their own, tighter ring than the terrain —
	 * swg.SnapshotLoadRadius / swg.SnapshotUnloadRadius. Game thread.
	 */
	void StartSnapshotResolve(const FIntPoint& Coord);

	/** Hands a tile's resolved objects to the per-frame spawner. Game thread. */
	void QueueSnapshotObjectsForTile(const FIntPoint& Coord, FSWGTerrainTile& Tile, TArray<FSWGWorldSnapshotSpawnInfo>&& Objects);

	/**
	 * Spawns queued .ws objects, nearest tile first, until
	 * swg.SnapshotSpawnBudgetMs of game-thread time is used (always at least
	 * one). A city tile lands hundreds of actors; doing them all the frame the
	 * bake finished was a visible hitch. Every frame, from Tick.
	 */
	void SpawnPendingSnapshotObjects();

	/** Takes down a tile's snapshot actors: rooms, object-graph registrations, terrain edits, then the actors. Game thread. */
	void DestroySnapshotActors(FSWGTerrainTile& Tile);

private:
	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> TreSubsystem;

	UPROPERTY()
	TObjectPtr<USWGMeshGeneratorSubsystem> MeshGenerator;

	/** Lazily loaded on first use — see ResolveWorldSnapshotNode. Same DataTable SWGInitializationState uses for CRC dispatch. */
	UPROPERTY()
	TObjectPtr<UDataTable> FormTagMappingTable;

	/** Parent material for BuildTerrainTileMaterial's per-tile MIDs — a simple vertex-color blend of up to 4 texture parameters (Layer0..Layer3). Authored as a plugin content asset, not generated at runtime. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TerrainBlendMaterial;

	/** TRE virtual path's texture (e.g. "rock_cliff_anza" -> texture/rock_cliff_anza.dds) -> decoded transient UTexture2D. See GetOrLoadShaderTexture. */
	UPROPERTY()
	TMap<FString, TObjectPtr<UTexture2D>> LoadedShaderTextures;

	/** Root every tile component attaches to, at the raw-space origin. */
	UPROPERTY()
	TObjectPtr<AActor> TerrainMeshActor;

	/** Components of unloaded tiles, kept registered but empty for the next tile to reuse. */
	UPROPERTY()
	TArray<TObjectPtr<UDynamicMeshComponent>> PooledTileComponents;

	// The parsed planet, immutable once loaded. Shared into every bake job so
	// GetHeightAt on the game thread and a dozen workers can all read it at
	// once with nothing ever writing to it.
	TSharedPtr<const FSWGTerrainData, ESPMode::ThreadSafe> PlanetData;
	bool bTerrainDataCached = false;

	// Runtime edits. OwnedEditLayers is the game thread's editable record;
	// PublishedEditLayers is the immutable copy jobs capture (rebuilt by
	// PublishEditLayers), so an append never reallocates under a worker.
	TArray<FSWGOwnedTerrainLayer> OwnedEditLayers;
	TSharedPtr<const TArray<FSWGTerrainLayer>, ESPMode::ThreadSafe> PublishedEditLayers;
	TArray<FSWGTerrainHole> TerrainHoles;
	int32 EditVersion = 0;

	/** Modifications requested before the terrain finished loading — see FSWGPendingTerrainModification. */
	TArray<FSWGPendingTerrainModification> PendingObjectModifications;

	/** Owners already stamped, so a re-spawned building doesn't stack a second pad. */
	TSet<int64> StampedEditOwners;

	// Live tiles. Components are owned by TerrainMeshActor (and so reachable
	// by GC through it); this map is the game thread's bookkeeping only.
	TMap<FIntPoint, FSWGTerrainTile> Tiles;

	/** Tiles wanted but not yet baking — PumpBakeQueue takes the nearest. */
	TSet<FIntPoint> BakeQueue;
	int32 BakesInFlight = 0;

	// Bumped every time the zone changes. A bake that lands after a zone change
	// carries the old value and is discarded rather than written into the new
	// zone's tiles.
	int32 TerrainGeneration = 0;

	FString ActiveTerrainVirtualPath;

	/** Where the player arrived; streams from here until a pawn exists. Raw space. */
	FVector2D SpawnRawPosition = FVector2D::ZeroVector;

	/** Last sweep's centre, for the heading bias. Raw space. */
	FVector2D LastStreamingCenter = FVector2D::ZeroVector;
	bool bHasLastStreamingCenter = false;

	/** The 3x3 around the spawn; OnTerrainReady fires once all are live. */
	TSet<FIntPoint> InitialTiles;
	bool bInitialTilesReported = false;

	float TimeUntilNextSweep = 0.0f;

	// The parsed .ws for this zone plus its top-level node indices bucketed by
	// the tile their position falls in. Read-only after LoadTerrain.
	TSharedPtr<const FSWGWorldSnapshotData, ESPMode::ThreadSafe> SnapshotData;
	TMap<FIntPoint, TArray<int32>> SnapshotNodesByTile;

	// ── Tuning ───────────────────────────────────────────────────────────

	// Resolution is constrained: ULandscapeComponent requires SubsectionSizeQuads+1
	// to be a power of two (LandscapeComponent.h:449) — 128 samples = 127 quads.
	static constexpr int32 HeightmapResolution = 128; // samples per axis

	// 512 m: a power-of-two multiple of retail's 8 m chunk, so tile edges line up
	// with the map origin and retail's own chunk grid. 127 quads over 512 m is
	// ~4 m a quad — every second retail pole (FSWGTerrainHeader::GetPoleSpacing).
	static constexpr float HeightmapWorldExtent = 512.0f; // world units per axis

	// Chebyshev tile radii around the player come from swg.TerrainLoadRadius /
	// swg.TerrainUnloadRadius (defaults 3 and 4: a 7x7, 3.5 km square, with
	// unload one tile further so pacing at a boundary can't thrash).

	// A bake is ~13 ms plus triangulation; four at once keeps a burst (zone
	// entry, 49 tiles) under a second without starving the mesh generator's pool.
	static constexpr int32 MaxBakesInFlight = 4;

	static constexpr float StreamingSweepInterval = 0.5f;

	// Hole-edge quads are split this many ways per axis — 0.5 units at the
	// current spacing. Sub-quads are kept by their centre, which is what
	// FSWGBuildingSpawnHandler's hole margin compensates for.
	static constexpr int32 TerrainQuadSubdivisions = 8;
};
