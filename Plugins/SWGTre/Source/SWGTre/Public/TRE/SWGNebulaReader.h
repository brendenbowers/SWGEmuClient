#pragma once

#include "CoreMinimal.h"

struct FSWGDataTableData;

struct FSWGNebulaData
{
	FString Name;
	FVector NativePosition = FVector::ZeroVector;
	float Radius = 0;
	float Density = 0;
	float FacingPercent = 0;
	FLinearColor FacingColor = FLinearColor::Black;
	FLinearColor OrientedColor = FLinearColor::Black;
	/** Flashes per second; zero means the nebula has no lightning. */
	float LightningFrequency = 0;
	float LightningDurationMax = 0;
	/** Flash colour at its start and, as it fades, at its end. Alpha is stored as authored. */
	FLinearColor LightningColor = FLinearColor::Black;
	FLinearColor LightningRampColor = FLinearColor::Black;
	FString LightningAppearance;
};

class SWGTRE_API FSWGNebulaReader
{
public:
	static bool Read(const FSWGDataTableData& Table, TArray<FSWGNebulaData>& Out);
};
