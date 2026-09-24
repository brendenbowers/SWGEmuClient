#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGMapMarkerWidget.generated.h"

class UButton;
class UTextBlock;

/** One pin on a USWGPlanetMapWidget. Raw space position (metres, x east, y north). */
USTRUCT(BlueprintType)
struct SWGUI_API FSWGMapMarker
{
	GENERATED_BODY()

	/** Reported back by the map's click events; unique within its layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	FText Label;

	/** Free-form look hint for the marker widget ("Player", "Waypoint", ...). The default widget draws "Player" as a heading arrow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	FName Style;

	/** Retail's travel-point label green unless set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	FLinearColor LabelColor = FLinearColor::FromSRGBColor(FColor(0x62, 0xFF, 0x15));

	/** Drawn in retail's activated colour instead of its idle one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	bool bSelected = false;

	/** Overrides the pin's idle colour (a waypoint's own colour); otherwise retail's orange. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	bool bCustomPinColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map", meta = (EditCondition = "bCustomPinColor"))
	FLinearColor PinColor = FLinearColor::White;

	/** The map feeds the widget Heading minus its own camera yaw each frame (ApplyScreenHeading). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	bool bHasHeading = false;

	/** Degrees clockwise from north. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map", meta = (EditCondition = "bHasHeading"))
	float Heading = 0.f;

	/** Everything but Position/Heading, which change per frame without a restyle. */
	bool LooksLike(const FSWGMapMarker& Other) const;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FSWGOnMapMarkerWidgetClicked, class USWGMapMarkerWidget*);

/**
 * How one marker looks on the map. The map creates one per marker from its
 * MarkerWidgetClass (or a per-layer override) and keeps it on the marker's
 * point. Subclass in Blueprint to restyle: lay out a designer tree (binding
 * PinButton/LabelText if you want the defaults), override ApplyMarker to
 * react to the data, and call NotifyClicked from any control you like.
 * Used from C++ alone it builds retail's travel pin with a green label.
 */
UCLASS(Blueprintable)
class SWGUI_API USWGMapMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The marker this widget shows; set before ApplyMarker runs. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Map")
	FSWGMapMarker Marker;

	/** The point within this widget (0-1 each axis) that sits on the marker's position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|Map")
	FVector2D Anchor = FVector2D(0.5f, 1.f);

	/** Called whenever the map assigns or restyles this widget's marker. */
	UFUNCTION(BlueprintNativeEvent, Category = "SWGEmu|Map")
	void ApplyMarker(const FSWGMapMarker& InMarker);

	/** Each frame for markers with bHasHeading: degrees clockwise on screen. */
	UFUNCTION(BlueprintNativeEvent, Category = "SWGEmu|Map")
	void ApplyScreenHeading(float ScreenDegrees);

	/** Reports this marker clicked to the map. The default pin calls it from PinButton. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Map")
	void NotifyClicked();

	FSWGOnMapMarkerWidgetClicked OnClicked;

	/** The layer the map placed this in; set by the map. */
	FName LayerName;

protected:
	virtual void NativeOnInitialized() override;

	/** Optional: the clickable pin. Built when the class has no designer tree. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Map", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PinButton;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Map", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	/** Optional: rotated by ApplyScreenHeading. The default is an arrow glyph shown for Style "Player". */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Map", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> HeadingIndicator;

private:
	UFUNCTION()
	void HandlePinClicked();
};
