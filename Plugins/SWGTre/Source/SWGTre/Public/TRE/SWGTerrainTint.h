#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGTerrainReader.h"

/** What the colour affectors (ACCN/ACRH/ACRF) and AEXC left at one ground point. */
struct FSWGTerrainGroundSample
{
	/** Retail's colour map; starts white and is the terrain vertex colour. */
	FColor Tint = FColor::White;
	/** AEXC marked the point. */
	bool bExcluded = false;
};

/** Retail ground-tint maths (SWGEmu.exe, see Docs/RetailTerrainNotes.md session 5). */
namespace SWGTerrainTint
{
	/**
	 * 0xb8a4f0: blends NewColour into Existing at Amount. Amount below 1 first scales NewColour per channel (truncating).
	 * Operation 1 adds that to Existing (clamped), 2 subtracts it (clamped), 3 lerps Existing towards the average of
	 * both by Amount, 4 is fatal in retail (Existing is returned), anything else lerps Existing towards NewColour.
	 */
	SWGTRE_API FColor Blend(const FColor& Existing, const FColor& NewColour, int32 Operation, float Amount);

	/** 0xb8aca0 with the ACRH/ACRF index: pixel trunc(Fraction * (Width - 1)) of the first row; false when outside the image. */
	SWGTRE_API bool SampleRamp(const FSWGTerrainColorRamp& Ramp, float Fraction, FColor& OutColor);

	/** Uncompressed or RLE truecolour TGA (24/32 bpp); keeps the top row. */
	SWGTRE_API bool DecodeTga(TConstArrayView<uint8> Bytes, FSWGTerrainColorRamp& OutRamp);

	/** Every distinct ramp path the planet's colour-ramp affectors reference (including children). */
	SWGTRE_API void CollectRampPaths(const FSWGTerrainData& Data, TArray<FString>& OutPaths);
}
