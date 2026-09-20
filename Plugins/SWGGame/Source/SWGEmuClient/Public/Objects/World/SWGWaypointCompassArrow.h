#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWGWaypointCompassArrow.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * A single retail path-arrow (appearance/path_arrow.msh — see
 * ASWGWaypointMarker) that stays under the player and always points at
 * whichever active waypoint is nearest, regardless of whether that
 * waypoint's own ASWGWaypointMarker/ground trail can be shown yet (this
 * needs only a bearing, not loaded terrain at the waypoint's end) — a
 * compass needle under the player's feet.
 *
 * Owned by USWGWaypointSubsystem: spawned the moment any waypoint is
 * active, repositioned onto the player and re-aimed every Tick, destroyed
 * again once none are.
 */
UCLASS()
class SWGEMUCLIENT_API ASWGWaypointCompassArrow : public AActor
{
	GENERATED_BODY()

public:
	ASWGWaypointCompassArrow();

	/** Absolute world yaw to point along — 0 already lands on raw north, same as any other bearing in this codebase (see Common/SWGWorldScale.h). */
	void SetHeadingDegrees(float YawDegrees);

	/**
	 * Tints the arrow for the waypoint's colour (see USWGWaypointSubsystem::
	 * GetWaypointColor) via the retail material's own TintColor/TintColor2
	 * hue-shift hookup (USWGMeshGeneratorSubsystem's palette-recolour
	 * mechanism) — best-effort: a shader with no such parameters just
	 * ignores the set silently and the arrow keeps its natural look.
	 */
	void SetColor(FLinearColor Color);

	/** Uniform scale applied to the (retail-sized, meant for close-up ground detail) arrow mesh. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Waypoint")
	float ArrowScale = 0.5f;

	/** How far ahead of the player (along the heading) the arrow's tail sits, so it doesn't overlap the character model. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Waypoint")
	float ForwardOffset = 60.f;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "SWGEmu|Waypoint")
	TObjectPtr<UStaticMeshComponent> Arrow;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ArrowMIDs;

	FLinearColor PendingColor = FLinearColor::White;
};
