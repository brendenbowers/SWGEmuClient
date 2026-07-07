#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TRE/SWGTerrainReader.h"
#include "SWGTerrainSubsystem.generated.h"

class USWGTreSubsystem;
class ALandscape;

/** Result of BakeHeightmap (or a cache hit) — everything SpawnLandscape needs. */
struct FSWGBakedHeightmap
{
	/** Row-major, HeightmapResolution x HeightmapResolution. Raw float heights — Landscape's uint16 encoding happens in SpawnLandscape. */
	TArray<float> Heights;

	/** World-space (X,Y) of Heights[0] — the grid's min corner, not the center. */
	FVector Origin = FVector::ZeroVector;

	/** World units between adjacent heightmap samples. */
	float Spacing = 0.0f;
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
class SWGEMU_API USWGTerrainSubsystem : public UGameInstanceSubsystem
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

private:
	void Error(const FString& ErrorMessage);

	void LoadTerrain(const FString& TerrainVirtualPath, const FVector& SpawnPosition);

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

	/** ti6: spawn one ALandscape actor at the whole grid's min corner (game thread). */
	ALandscape* SpawnLandscapeActor(const FVector& GridOrigin, float Spacing);

	/** ti6: build/register one component from a baked heightmap at the given SectionBase (quad units, game thread). */
	void AddLandscapeComponent(ALandscape* Landscape, const FSWGBakedHeightmap& Heightmap, const FIntPoint& SectionBase);

	/** ti6: spawn the actor + every component in the grid (game thread). */
	void SpawnLandscapeGrid(const TArray<FSWGBakedHeightmap>& Grid, const FVector& GridOrigin, float Spacing);

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> TreSubsystem;

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

	// Tile ComponentGridSize x ComponentGridSize components (each still
	// HeightmapResolution samples / HeightmapWorldExtent world units, same as a
	// single patch) under one ALandscape actor, centered on the spawn point —
	// more coverage without coarsening spacing. Kept odd so there's a well-defined
	// center component containing the spawn point.
	static constexpr int32 ComponentGridSize = 3;
};
