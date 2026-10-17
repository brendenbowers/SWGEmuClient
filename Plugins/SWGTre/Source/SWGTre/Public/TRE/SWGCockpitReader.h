#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

struct SWGTRE_API FSWGCockpitData
{
	FString FrameAppearance;
	TArray<float> ZoomDistances;
	float FirstPersonThreshold = 0;
	FVector3f NativeThirdPersonOffset = FVector3f::ZeroVector;
	FVector3f NativeFirstPersonOffset = FVector3f::ZeroVector;
	TOptional<FVector3f> NativePobPosition;
};

class SWGTRE_API FSWGCockpitReader
{
public:
	/** CPIT/0000. Retains authored distances and native XYZ without camera-mode assumptions. */
	static bool Read(const FSWGIffReader& Reader, FSWGCockpitData& Out);
};
