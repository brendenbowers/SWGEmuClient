#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

struct FSWGPlanetSurface
{
	float SpinDegreesPerSecond = 0;
	FString Shader;
	float Radius = 0;
	FVector2f UVRepeat = FVector2f(1, 1);
	FVector2f DetailUVRepeat = FVector2f::ZeroVector;
};

struct FSWGPlanetAppearanceData
{
	int32 LatitudeSegments = 0;
	int32 LongitudeSegments = 0;
	FSWGPlanetSurface Surface;
	FSWGPlanetSurface Clouds;
	FString HaloShader;
	float HaloRadiusRatio = 0;
};

class SWGTRE_API FSWGPlanetAppearanceReader
{
public:
	static bool Read(const FSWGIffReader& Reader, FSWGPlanetAppearanceData& Out);
};
