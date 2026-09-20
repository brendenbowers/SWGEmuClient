#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGWaypointMarkerWidget.generated.h"

class UCanvasPanel;
class UImage;
class UTextBlock;
struct FSWGWaypointEntry;

/**
 * The 2D half of a waypoint's on-screen presence — the 3D half is
 * ASWGWaypointMarker, USWGWaypointSubsystem's spinning beacon actor. While a
 * waypoint's beacon is actually visible on screen (and its ground streamed
 * in), this just floats a name/distance label above it. The rest of the
 * time — off-screen, behind the camera, or too far for terrain/beacon to
 * have loaded — it draws an edge-clamped directional arrow instead,
 * camera-relative so it still works with the target directly behind the
 * player. A full-screen, click-through layer inside WBP_Hud, built the same
 * way WBP_FloatingText is: the Blueprint contributes only the root Canvas,
 * every marker is built in code.
 */
UCLASS(Abstract)
class SWGUI_API USWGWaypointMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** World-space Z lift so the pin sits above the ground rather than in it. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Waypoint")
	float MarkerHeightAboveGround = 150.f;

	/** Icon size, in screen pixels, both pinned and arrow forms. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Waypoint")
	FVector2D IconSize = FVector2D(28.f, 28.f);

	/** How far inside the viewport edge an off-screen arrow sits. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Waypoint")
	float EdgeMargin = 48.f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Full-screen canvas the markers are placed on (WBP_WaypointMarkers / the HUD's own canvas). */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> Canvas;

private:
	struct FMarkerWidgets
	{
		TObjectPtr<UImage> Icon;
		TObjectPtr<UTextBlock> Label;
	};

	/** Builds (or reuses) the icon+label pair for one waypoint. */
	FMarkerWidgets& FindOrCreateMarker(int64 WaypointObjectId);

	/** Positions/rotates/labels one marker for this frame's entry; returns false if it should be hidden entirely (e.g. no player controller). */
	void UpdateMarker(FMarkerWidgets& Marker, const FSWGWaypointEntry& Entry, const FVector2D& ViewportSize);

	UFUNCTION()
	void HandleWaypointListChanged();

	TMap<int64, FMarkerWidgets> Markers;
};
