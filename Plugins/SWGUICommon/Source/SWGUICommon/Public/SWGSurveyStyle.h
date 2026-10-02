#pragma once

#include "CoreMinimal.h"
#include "SWGMapGroundOverlay.h"
#include "SWGMapMarker.h"

struct FSWGSurveyResult;

/** Looks shared by the survey window and the survey hologram. */
namespace SWGSurveyStyle
{
	/** Cold to hot: dark blue, cyan, yellow, orange-red for density 0-1. Alpha rises with density. */
	SWGUICOMMON_API FLinearColor DensityColor(float Density);

	/** "81%" as retail's map labels read. */
	SWGUICOMMON_API FText DensityText(float Density);

	/** The result's grid resampled to a smooth Resolution x Resolution field, coloured by DensityColor. */
	SWGUICOMMON_API FSWGMapGroundOverlay MakeOverlay(const FSWGSurveyResult& Result, int32 Resolution = 24, float Opacity = 0.55f);

	/** Map layer every view draws a scan in. */
	const FName MarkerLayer(TEXT("Survey"));

	/** Marker style for the point the server's waypoint goes on. */
	const FName BestStyle(TEXT("Best"));

	/**
	 * One marker per grid point, in the density colour (pin and label), the best
	 * point styled BestStyle. bLabelEveryPoint false labels only the best, for
	 * views where two dozen percentages would crowd out everything else.
	 */
	SWGUICOMMON_API TArray<FSWGMapMarker> MakeMarkers(const FSWGSurveyResult& Result, bool bLabelEveryPoint = true);
}
