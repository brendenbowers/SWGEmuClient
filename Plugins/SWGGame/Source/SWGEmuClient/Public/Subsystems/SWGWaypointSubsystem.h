#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "SWGWaypointSubsystem.generated.h"

class USWGObjectGraphSubsystem;
class USWGTerrainSubsystem;
class USWGJournalComponent;
class USWGMissionSubsystem;
class USWGCommandSubsystem;
class USWGTreSubsystem;
class ASWGWaypointMarker;
class ASWGWaypointCompassArrow;

/** SWG's fixed waypoint colour palette (WaypointObject's Colour byte). */
UENUM(BlueprintType)
enum class ESWGWaypointColor : uint8
{
	White = 0,
	Blue = 1,
	Green = 2,
	Orange = 3,
	Yellow = 4,
	Purple = 5
};

/** One entry from the local player's datapad waypoint list, ready for a UI or world marker to read. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGWaypointEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Waypoint")
	int64 WaypointObjectId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Waypoint")
	FText Name;

	// int32, not uint32: uint32 isn't Blueprint-exposable — matches FSWGMissionEntry::TypeCRC's own convention for a CRC field.
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Waypoint")
	int32 PlanetCRC = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Waypoint")
	ESWGWaypointColor Color = ESWGWaypointColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Waypoint")
	bool bActive = false;

	/** Non-zero when the waypoint sits inside a building cell rather than the open world — no ASWGWaypointMarker is placed for these yet. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Waypoint")
	int32 CellId = 0;

	/** Raw (SWG-space, x east/y north/z up) position — see Common/SWGWorldScale.h. */
	UPROPERTY()
	FVector RawPosition = FVector::ZeroVector;

	/** Distance from the player to RawPosition, in meters. Only meaningful when bHasDistance is set. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Waypoint")
	float DistanceMeters = 0.f;

	/** Compass bearing from the player to the waypoint ("N", "NE", ...), or empty if unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Waypoint")
	FString Direction;

	/** 0-360, north-relative clockwise bearing behind Direction. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Waypoint")
	float BearingDegrees = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Waypoint")
	bool bHasDistance = false;

	/** Whether the terrain tile under RawPosition is currently streamed in — gates whether a world-space marker can be placed for it. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Waypoint")
	bool bIsWorldLoaded = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnWaypointListChanged);

/**
 * Tracks the local player's waypoints and turns them into something a
 * waypoint window or world/compass indicator can read every frame: live
 * distance and compass bearing from the player, and whether the waypoint's
 * ground is currently streamed in.
 *
 * Two sources feed this:
 *  - USWGJournalComponent::WaypointList (PLAY base8) — a compact embedded
 *    list on the player's own object, polled every PollInterval. Empty on
 *    at least some Core3 builds (see swg.DumpWaypoints) — not the one that
 *    actually carries mission waypoints.
 *  - Each mission's own embedded waypoint (FSWGMissionEntry::bHasWaypoint
 *    and friends) — MissionObjectMessage3's field index 0x10, already fully
 *    decoded by USWGMissionSubsystem (see MissionObjectBaseline.h's
 *    Waypoint* fields) but never surfaced anywhere until now. This is where
 *    a mission's granted waypoint actually shows up.
 * Both feed the same GetWaypoints(), keyed by WaypointObjectId so either
 * (or both, redundantly) populating an entry is harmless.
 *
 * Mirrors USWGMissionSubsystem's shape (a GameInstanceSubsystem exposing a
 * GetX() snapshot recomputed on each call).
 *
 * Activation is toggled server-side through setWaypointActiveStatus; the
 * returned PLAY delta remains the authority for each waypoint's active flag.
 *
 * Also owns the 3D side of an active, loaded waypoint: it spawns/moves/
 * destroys one ASWGWaypointMarker per such waypoint (FTickableGameObject,
 * so this happens every frame without needing a widget or anything else to
 * drive it) and periodically refreshes each marker's ground arrow trail —
 * a real path from the navigation system when one resolves, a single arrow
 * hint toward the waypoint when it doesn't (out of navmesh range, or none
 * generated there yet).
 *
 * And, independent of any of that (it needs only a bearing, not loaded
 * terrain at the waypoint's end): a single ASWGWaypointCompassArrow that
 * stays under the player and always points at the nearest active waypoint —
 * see UpdateCompassArrow.
 */
UCLASS()
class SWGEMUCLIENT_API USWGWaypointSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/** Fired whenever the underlying waypoint list changes (added/removed/renamed/active toggled). */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Waypoint")
	FSWGOnWaypointListChanged OnWaypointListChanged;

	/** Every waypoint in the player's datapad, with live distance/bearing/streamed-state filled in. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Waypoint")
	TArray<FSWGWaypointEntry> GetWaypoints() const;

	/** GetWaypoints(), filtered to bActive. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Waypoint")
	TArray<FSWGWaypointEntry> GetActiveWaypoints() const;

	/** Asks the server to toggle this waypoint's active state. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Waypoint")
	bool ToggleWaypoint(int64 WaypointObjectId);

	/** UI colour for a waypoint's Colour byte. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Waypoint")
	static FLinearColor GetWaypointColor(ESWGWaypointColor Color);

private:
	void PollForChanges();

	UFUNCTION()
	void HandleMissionListChanged();

	USWGJournalComponent* FindJournalComponent() const;

	/** Pulls every mission with a granted waypoint out of USWGMissionSubsystem::GetMissions(), converted to FSWGWaypointEntry. */
	TArray<FSWGWaypointEntry> GetMissionWaypoints() const;

	/** Spawns/moves/destroys each active+loaded waypoint's ASWGWaypointMarker; tears down markers for anything no longer active+loaded. Called every Tick. */
	void UpdateMarkers(const TArray<FSWGWaypointEntry>& ActiveWaypoints);

	/** Keeps the single player-following ASWGWaypointCompassArrow at the player's feet, aimed at the nearest active waypoint (any of them — doesn't need bIsWorldLoaded); destroys it once none are active. Called every Tick. */
	void UpdateCompassArrow(const TArray<FSWGWaypointEntry>& ActiveWaypoints);

	/**
	 * Recomputes one marker's ground arrow trail toward WaypointLocation from
	 * the player's current position: a real navmesh path when
	 * UNavigationSystemV1 resolves one — an arrow at each path point, facing
	 * the next — or, when it doesn't (out of navmesh range, or none
	 * generated there yet), a single arrow a short distance ahead of the
	 * player, facing the waypoint. A fake straight-line trail all the way to
	 * a waypoint that might be kilometers away would be misleading; one
	 * directional hint isn't. World space (UE units) throughout.
	 */
	void RefreshBreadcrumbs(ASWGWaypointMarker& Marker, const FVector& PlayerLocation, const FVector& WaypointLocation) const;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGTerrainSubsystem> Terrain;

	UPROPERTY()
	TObjectPtr<USWGMissionSubsystem> Missions;

	UPROPERTY()
	TObjectPtr<USWGCommandSubsystem> Commands;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	/** Snapshot of the journal's waypoints as of the last PollForChanges — identity/name/colour/active only, compared to detect change. */
	TArray<FSWGWaypointEntry> CachedEntries;

	FTimerHandle PollTimer;

	/** How often PollForChanges checks the journal component for changes. */
	static constexpr float PollInterval = 0.5f;

	/** One live 3D marker per active+loaded waypoint id — see UpdateMarkers. */
	UPROPERTY()
	TMap<int64, TObjectPtr<ASWGWaypointMarker>> Markers;

	/** Next time (World->GetTimeSeconds()) each marker's breadcrumb trail should be recomputed — pathfinding isn't cheap enough to do every frame. */
	TMap<int64, double> NextBreadcrumbRefreshTime;

	static constexpr float BreadcrumbRefreshInterval = 2.0f;
	static constexpr float BreadcrumbSpacing = 300.f;
	static constexpr float BreadcrumbFallbackDistance = 3000.f;

	/** The single player-following compass arrow — see UpdateCompassArrow. Null whenever no waypoint is active. */
	UPROPERTY()
	TObjectPtr<ASWGWaypointCompassArrow> CompassArrow;

	/** Lift above the player's feet so the arrow doesn't z-fight the ground. */
	static constexpr float CompassArrowGroundOffset = 5.f;

	/** Off for now — USWGWaypointMarkerWidget's screen-space label/edge-arrow is considered enough on its own. See UpdateCompassArrow. */
	static constexpr bool bCompassArrowEnabled = false;
};
