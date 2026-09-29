#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGMapGroundOverlayWidget.generated.h"

class USWGPlanetMapWidget;

/** A colour field laid on the ground of a map (a survey's concentrations). Raw metres, x east, y north. */
USTRUCT(BlueprintType)
struct SWGUI_API FSWGMapGroundOverlay
{
	GENERATED_BODY()

	/** South-west corner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	FVector2D Min = FVector2D::ZeroVector;

	/** North-east corner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	FVector2D Max = FVector2D::ZeroVector;

	/** Vertices per row; at least 2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	int32 Columns = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	int32 Rows = 0;

	/** Columns x Rows vertex colours, row 0 along the north edge; alpha fades the field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	TArray<FLinearColor> Colors;

	/** Border drawn round the field; transparent draws none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	FLinearColor OutlineColor = FLinearColor::Transparent;

	bool IsValid() const { return Columns >= 2 && Rows >= 2 && Colors.Num() == Columns * Rows && Max.X > Min.X && Max.Y > Min.Y; }
};

/**
 * Paints an FSWGMapGroundOverlay onto its map's terrain as a vertex-coloured
 * mesh, each vertex projected through the map camera so it follows pan, orbit
 * and relief. USWGPlanetMapWidget puts it under its marker layers.
 */
UCLASS(NotBlueprintable)
class SWGUI_API USWGMapGroundOverlayWidget : public UUserWidget
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
