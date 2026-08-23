#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

enum class ESWGShaderTextureUsage : uint8
{
	Unknown,
	Diffuse,
	Normal,
	Specular,
	Lightmap,
	// REP0: a decal slot the runtime substitutes per-object (effect names
	// carry "replace0"). The texture stored in the .sht is only a placeholder
	// — resource containers ship texture/test_ui_res_cereal.dds here.
	Replaceable
};

struct FSWGShaderTexture
{
	FString Tag;
	FString VirtualPath;
	ESWGShaderTextureUsage Usage = ESWGShaderTextureUsage::Unknown;
	/** Which mesh UV set samples this texture, from FORM TCSS. Decal slots
	 *  typically sit on UV1 while MAIN stays on UV0. */
	uint8 TexCoordSet = 0;
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

	/**
	 * The .eft render-effect name referenced by this shader (e.g.
	 * "effect\h_alpha_color2_specmap_cbmp.eft"). SWG's "alpha" effects (the
	 * naming convention actually used by the shipped shader set — no other
	 * distinguishing flag was found in the SSHT chunks we parse) render as
	 * alpha-masked/blended rather than opaque; anything without "alpha" in
	 * the effect name is a normal opaque surface. Used to pick between
	 * M_SWGObjectTextured and M_SWGObjectTexturedMasked in
	 * GetOrBuildObjectMaterial.
	 */
	FString EffectName;

	bool NeedsAlphaBlend() const { return EffectName.Contains(TEXT("alpha"), ESearchCase::IgnoreCase); }

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
