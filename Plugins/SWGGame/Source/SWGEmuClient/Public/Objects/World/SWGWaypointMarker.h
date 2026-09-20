#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWGWaypointMarker.generated.h"

class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class URotatingMovementComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * The 3D world-space stand-in for one datapad waypoint: a slowly spinning
 * beacon at its exact location, plus retail's own ground path-arrow mesh
 * (appearance/path_arrow.msh — the same asset the newbie tutorial's "follow
 * the arrow" guidance used) leading toward it: a real trail of arrows along
 * whatever route USWGWaypointSubsystem last found via the navigation system,
 * or just one arrow pointing at it when no path resolves (out of navmesh
 * range, or none generated there yet) — see
 * USWGWaypointSubsystem::RefreshBreadcrumbs. A straight-line trail across
 * ground the player hasn't necessarily seen would be misleading, but a
 * single directional hint isn't.
 *
 * Spawned and destroyed entirely by USWGWaypointSubsystem — only while its
 * waypoint is active and its ground tile is streamed in (see
 * FSWGWaypointEntry::bIsWorldLoaded), so one never sits over unloaded
 * terrain or a deactivated/removed waypoint.
 */
UCLASS()
class SWGEMUCLIENT_API ASWGWaypointMarker : public AActor
{
	GENERATED_BODY()

public:
	ASWGWaypointMarker();

	/** Tints the beacon and, best-effort, the ground trail arrows (see ASWGWaypointCompassArrow::SetColor — same TintColor/TintColor2 hookup, silently ignored if the shader has neither). */
	void SetColor(FLinearColor Color);

	/**
	 * Replaces the ground arrow trail. WorldTransforms are absolute UE
	 * world-space (location at ground height, rotation facing the direction
	 * of travel at that point) — nearest the player first. An empty array
	 * just clears the trail.
	 */
	void SetBreadcrumbPoints(const TArray<FTransform>& WorldTransforms);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "SWGEmu|Waypoint")
	TObjectPtr<UStaticMeshComponent> Beacon;

	UPROPERTY(VisibleAnywhere, Category = "SWGEmu|Waypoint")
	TObjectPtr<URotatingMovementComponent> BeaconSpin;

	UPROPERTY(VisibleAnywhere, Category = "SWGEmu|Waypoint")
	TObjectPtr<UInstancedStaticMeshComponent> Breadcrumbs;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BeaconMID;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BreadcrumbMIDs;

	FLinearColor PendingColor = FLinearColor::White;

	/** Queued if SetBreadcrumbPoints is called before the path_arrow mesh request resolves. */
	TArray<FTransform> PendingBreadcrumbTransforms;
	bool bBreadcrumbMeshReady = false;
};
