#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

enum class ESWGShaderTextureUsage : uint8
{
	Unknown,
	Diffuse,
	Normal,
	Specular,
	Lightmap
};

struct FSWGShaderTexture
{
	FString Tag;
	FString VirtualPath;
	ESWGShaderTextureUsage Usage = ESWGShaderTextureUsage::Unknown;
	uint8 AddressU = 0;
	uint8 AddressV = 0;
	uint8 AddressW = 0;
	uint8 MipFilter = 0;
	uint8 MinFilter = 0;
	uint8 MagFilter = 0;
};

struct SWGTRE_API FSWGShaderData
{
	TArray<FSWGShaderTexture> Textures;

	const FSWGShaderTexture* FindTexture(ESWGShaderTextureUsage Usage) const;
};

/** Reads the render-relevant parts of FORM SSHT shader templates. */
class SWGTRE_API FSWGShaderReader
{
public:
	static bool ReadShader(const FSWGIffReader& Reader, FSWGShaderData& OutShader);

private:
	static ESWGShaderTextureUsage ClassifyTextureTag(const FString& Tag);
};
