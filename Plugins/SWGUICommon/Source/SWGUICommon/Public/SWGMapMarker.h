#pragma once

#include "CoreMinimal.h"
#include "SWGMapMarker.generated.h"

/** One pin on a USWGPlanetMapWidget. Raw space position (metres, x east, y north). */
USTRUCT(BlueprintType)
struct SWGUICOMMON_API FSWGMapMarker
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
