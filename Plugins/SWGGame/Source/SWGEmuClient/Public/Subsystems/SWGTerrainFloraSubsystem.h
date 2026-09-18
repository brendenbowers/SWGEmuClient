#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "TRE/SWGTerrainFloraPlacer.h"
#include "SWGTerrainFloraSubsystem.generated.h"

class USWGMeshGeneratorSubsystem;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/**
 * Streams the .trn's procedural vegetation around the player: the four
 * tiers of FSWGTerrainHeader (collidable flora such as trees and boulders,
 * non-collidable clutter, near radial grass billboards, far radial tree
 * sprites), each as its own ring of FSWGTerrainFloraPlacer cells out to the
 * tier's authored MaxDistance. A cell is placed on a worker against the
 * terrain subsystem's immutable bake source, then lands as one instanced
 * static mesh component per appearance under a single flora actor;
 * appearances are resolved through USWGMeshGeneratorSubsystem's asset cache
 * once and shared by every cell that uses them.
 *
 * Follows USWGTerrainSubsystem's zone lifecycle through its OnTerrainLoaded
 * / OnZoneReset / OnTerrainEditsChanged signals, so a building pad landing
 * re-places the cells under it (their flora-clearing layers and holes apply
 * on the re-bake).
 *
 * swg.Flora (master), swg.FloraCollidable/NonCollidable/RadialNear/RadialFar
 * (per tier), swg.FloraDensityScale and swg.FloraDistanceScale tune it.
 */
UCLASS()
class SWGEMUCLIENT_API USWGTerrainFloraSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bTerrainLoaded; }

	/** swg.FloraStats — per-tier cell/instance counts, the flora sample at the player, and every appearance's state. */
	void DumpStats() const;

private:
	/** One placement cell of one tier — the streaming unit. */
	struct FCellKey
	{
		ESWGTerrainFloraTier Tier = ESWGTerrainFloraTier::Collidable;
		FIntPoint Coord = FIntPoint::ZeroValue;

		bool operator==(const FCellKey& Other) const { return Tier == Other.Tier && Coord == Other.Coord; }
		friend uint32 GetTypeHash(const FCellKey& Key) { return HashCombine(GetTypeHash(Key.Coord), (uint32)Key.Tier); }
	};

	/**
	 * A cell's instances that share one appearance, in UE world space, held
	 * until that appearance's mesh exists and then turned into a component.
	 */
	struct FInstanceGroup
	{
		/** appearance/<name>.apt for mesh flora; the bare shader name for radial billboards. */
		FString AppearanceKey;
		bool bRadial = false;
		TArray<FTransform> Transforms;
	};

	struct FCell
	{
		/** Live components under FloraActor, destroyed with the cell. */
		TArray<TObjectPtr<UInstancedStaticMeshComponent>> Components;

		/** Groups whose appearance mesh hasn't arrived yet — see OnAppearanceMeshReady. */
		TArray<FInstanceGroup> PendingGroups;

		/** Terrain edit version this cell was placed with; a newer edit overlapping it queues a re-place. */
		int32 PlacedEditVersion = -1;
		int32 WantedEditVersion = 0;

		bool bPlaceInFlight = false;
	};

	/** One resolved appearance, shared by every cell that places it. */
	struct FAppearanceMesh
	{
		TObjectPtr<UStaticMesh> Mesh;
		TArray<TObjectPtr<UMaterialInterface>> Materials;
		/** The billboard's height per metre of width (bMaintainAspectRatio) — 1 for meshes. */
		float HeightPerWidth = 1.0f;
		bool bRequested = false;
		bool bFailed = false;
	};

	// ── Lifecycle ───────────────────────────────────────────────────────

	void OnTerrainLoaded();
	void OnZoneReset();
	void OnTerrainEditsChanged(const FBox2D& RawBounds);

	/** Spawns the empty root every instanced component attaches to. */
	void SpawnFloraActor();

	/** Tears down every cell, queued job and resolved appearance. */
	void ResetAll();

	// ── Streaming ────────────────────────────────────────────────────────

	/** Raw-space centre streaming works from — the pawn once it exists. False before then. */
	bool GetStreamingCenter(FVector2D& OutRawPosition) const;

	/** Every 0.5 s: queues missing cells inside each enabled tier's ring, unloads cells outside it, pumps the queue. */
	void UpdateStreaming();

	/** Whether a tier's cell should currently exist for a centre at Center (its annulus, with a one-cell hysteresis when bForUnload). */
	bool IsCellWanted(ESWGTerrainFloraTier Tier, const FIntPoint& Coord, const FVector2D& Center, bool bForUnload) const;

	static bool IsTierEnabled(ESWGTerrainFloraTier Tier);

	/** Starts worker placements for queued cells, nearest first, up to MaxPlacementsInFlight. */
	void PumpPlaceQueue();

	void StartCellPlacement(const FCellKey& Key);

	/** A finished placement on the game thread — grouped by appearance and attached, or dropped if the cell is gone or stale. */
	void OnCellPlaced(const FCellKey& Key, int32 Generation, int32 EditVersion, TArray<FSWGTerrainFloraInstance>&& Instances);

	void UnloadCell(const FCellKey& Key);

	// ── Instancing ──────────────────────────────────────────────────────

	/** Groups a placed cell's instances by appearance into UE-space transforms. */
	void BuildInstanceGroups(ESWGTerrainFloraTier Tier, const TArray<FSWGTerrainFloraInstance>& Instances, TArray<FInstanceGroup>& OutGroups) const;

	/** Turns every pending group whose appearance has resolved into a component; requests the ones that haven't been yet. */
	void FlushPendingGroups(const FCellKey& Key, FCell& Cell);

	/** Kicks the mesh generator for an appearance the first time any cell asks for it. */
	void RequestAppearance(const FString& AppearanceKey, bool bRadial);

	/** Records a resolved (or failed) appearance, reading a billboard's aspect ratio off its diffuse texture. */
	void ResolveAppearance(FAppearanceMesh& Appearance, const FString& AppearanceKey, UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials);

	/** Mesh generator callback: every cell waiting on this appearance gets its component. */
	void OnAppearanceMeshReady(const FString& AppearanceKey, UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials);

	/** One ISM component for a group, attached under FloraActor with the tier's cull distance. */
	UInstancedStaticMeshComponent* CreateGroupComponent(ESWGTerrainFloraTier Tier, const FInstanceGroup& Group, const FAppearanceMesh& Appearance);

	/**
	 * The shared radial billboard: two unit (1 m) quads crossed at 90
	 * degrees, pivot at the bottom centre, both faces present so the
	 * back is drawn without a two-sided material. Built once through the
	 * mesh generator's asset cache; instances scale it to (width, width,
	 * height).
	 */
	UStaticMesh* GetOrBuildRadialBillboardMesh();

private:
	UPROPERTY()
	TObjectPtr<USWGTerrainSubsystem> TerrainSubsystem;

	UPROPERTY()
	TObjectPtr<USWGMeshGeneratorSubsystem> MeshGenerator;

	UPROPERTY()
	TObjectPtr<AActor> FloraActor;

	UPROPERTY()
	TObjectPtr<UStaticMesh> RadialBillboardMesh;

	/** The zone's immutable planet data, taken from the terrain subsystem at OnTerrainLoaded. */
	TSharedPtr<const FSWGTerrainData, ESPMode::ThreadSafe> Planet;

	TMap<FCellKey, FCell> Cells;
	TSet<FCellKey> PlaceQueue;
	int32 PlacementsInFlight = 0;

	/** Keyed like FInstanceGroup::AppearanceKey. */
	TMap<FString, FAppearanceMesh> Appearances;

	/** Bumped on every zone reset; a placement landing with an older value is discarded. */
	int32 Generation = 0;

	bool bTerrainLoaded = false;
	float TimeUntilNextSweep = 0.0f;

	static constexpr int32 MaxPlacementsInFlight = 3;
	static constexpr float StreamingSweepInterval = 0.5f;
};
