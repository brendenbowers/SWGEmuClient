#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGDataTableReader.h"

/** One datatables/clientregion/<planet>.iff row: Name ("@stf_table:key"), X, Z (world north), Radius, all metres. */
struct SWGEMUCLIENT_API FSWGClientRegion
{
	FString NameReference;
	FVector2D Center = FVector2D::ZeroVector;
	float Radius = 0.f;

	/** Strictly inside: retail ClientRegionSphere is a sphere test on the raw position. */
	bool Contains(const FVector2D& RawPosition) const;
};

/** All client regions of a planet with the lookup retail's ClientRegionManager sphere tree answers. */
struct SWGEMUCLIENT_API FSWGRegionSet
{
	TArray<FSWGClientRegion> Regions;

	/** Replaces Regions from a table with Name/X/Z/Radius columns; false (and empty) if a column is missing. */
	bool LoadFromTable(const FSWGDataTableData& Table);

	/** Index of the smallest region containing the point (a city inside a larger area wins), or INDEX_NONE. */
	int32 FindContaining(const FVector2D& RawPosition) const;

	/** Index of the nearest region whose edge is within Margin of the point (no-build style test), or INDEX_NONE. */
	int32 FindNear(const FVector2D& RawPosition, float Margin) const;
};
