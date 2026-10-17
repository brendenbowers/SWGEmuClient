#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

struct FSWGSpaceLight
{
	uint8 Flags = 0;
	FLinearColor Diffuse;
	FLinearColor Specular;
	FVector NativeVector = FVector::ZeroVector;
};

struct FSWGSpacePlanet
{
	FString Appearance;
	FVector NativePosition = FVector::ZeroVector;
	FVector NativeAnglesDegrees = FVector::ZeroVector;
	float HaloAngleDegrees = 0;
	float HaloScale = 0;
};

struct FSWGSpaceCelestial
{
	FString Shader;
	float Size = 0;
	FString GlowShader;
	float GlowSize = 0;
	FVector NativeDirection = FVector::ZeroVector;
};

struct FSWGSpaceTerrainData
{
	FLinearColor ClearColor = FLinearColor::Black;
	FLinearColor Ambient = FLinearColor::Black;
	uint8 SkyboxFlags = 0;
	FString Skybox;
	FString EnvironmentTexture;
	FString StarColorRamp;
	int32 StarCount = 0;
	int32 DustCount = 0;
	float DustRadius = 0;
	TArray<FSWGSpaceLight> Lights;
	TArray<FSWGSpacePlanet> Planets;
	TArray<FSWGSpaceCelestial> Celestials;
};

class SWGTRE_API FSWGSpaceTerrainReader
{
public:
	// Native light vectors are retained without assuming angles or world directions.
	static bool Read(const FSWGIffReader& Reader, FSWGSpaceTerrainData& Out);
};
