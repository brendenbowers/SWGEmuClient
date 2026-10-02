#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGMapGroundOverlay.h"
#include "SWGMapGroundOverlayWidget.generated.h"

class USWGPlanetMapWidget;


/**
 * Paints an FSWGMapGroundOverlay onto its map's terrain as a vertex-coloured
 * mesh, each vertex projected through the map camera so it follows pan, orbit
 * and relief. USWGPlanetMapWidget puts it under its marker layers.
 */
UCLASS(NotBlueprintable)
class SWGUIWINDOW_API USWGMapGroundOverlayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetMap(USWGPlanetMapWidget* InMap);
	void SetOverlay(const FSWGMapGroundOverlay& InOverlay) { Overlay = InOverlay; }
	const FSWGMapGroundOverlay& GetOverlay() const { return Overlay; }

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<USWGPlanetMapWidget> Map;
	FSWGMapGroundOverlay Overlay;
};
