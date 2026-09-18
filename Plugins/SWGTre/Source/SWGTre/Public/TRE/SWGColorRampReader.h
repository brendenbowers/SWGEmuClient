#pragma once

#include "CoreMinimal.h"

/**
 * A retail environment colour ramp (terrain/colorramp/<planet>_<env>0.tga):
 * an uncompressed 256x8 32-bit TGA whose columns run through the day cycle
 * and whose rows are the lighting colours the client fed its fixed-function
 * lights. Row meanings were read off corellia_global0.tga (2026-09-18):
 * row 0 is always lit (ambient), row 1 is zero at night and warm at noon
 * (sun diffuse), rows 5/6 carry the sunset-tinted horizon colours (fog and
 * sky), row 7 a dark tint (shadow); rows 3/4 are empty on every ramp seen.
 * Columns: the sun rises around column 16, peaks near 64 and sets by 128.
 */
struct SWGTRE_API FSWGColorRamp
{
	enum ERow : int32
	{
		Ambient = 0,
		SunDiffuse = 1,
		SunSpecular = 2,
		Fog = 5,
		Sky = 6,
		Shadow = 7,
		RowCount = 8
	};

	int32 Width = 0;
	/** Row-major, RowCount rows of Width colours. */
	TArray<FColor> Pixels;

	bool IsValid() const { return Width > 0 && Pixels.Num() == Width * RowCount; }

	/** Row colour at a 0..1 position through the day cycle, linearly blended between columns (wrapping). */
	FLinearColor Sample(ERow Row, float DayFraction) const;
};

class SWGTRE_API FSWGColorRampReader
{
public:
	/** Decodes an uncompressed true-colour TGA (types 2, 24/32 bpp, either row order). Returns false for anything else. */
	static bool ReadTga(const TArray<uint8>& Bytes, FSWGColorRamp& OutRamp);
};
