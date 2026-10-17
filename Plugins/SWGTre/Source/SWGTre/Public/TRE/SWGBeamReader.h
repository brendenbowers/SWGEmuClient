#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/**
 * appearance/beam_*.iff (FORM BEAM > 0000; 4 files: red, green, yellow, template): float length (10 m), float width
 * (0.3 m), RGBA floats (1,1,1,1), shader path. Layout confirmed; the length/width/colour names are inferred.
 */
struct FSWGBeam
{
	float Length = 0.f;
	float Width = 0.f;
	FLinearColor Color = FLinearColor::White;
	FString ShaderPath;
};

class SWGTRE_API FSWGBeamReader
{
public:
	static bool Read(const FSWGIffReader& Reader, FSWGBeam& OutBeam);
};
