#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGFootprintReader.h"

/** Core3 StructureManager::getStructureFootprint and its corner-containment overlap test. Raw X/north metres. */
struct SWGEMUCLIENT_API FSWGPlacementRules
{
	static FBox2D GetServerFootprintRect(const FSWGStructureFootprint& Footprint, int32 AngleDegrees);
	static bool IsInStructureFootprint(const FBox2D& Rect, const FVector2D& Point);
	static bool RectsConflict(const FBox2D& Placing, const FBox2D& Existing);
};
