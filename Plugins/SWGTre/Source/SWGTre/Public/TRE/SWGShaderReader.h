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

	/**
	 * FORM ARVS: the shader's alpha-test reference values, keyed by the
	 * (un-reversed) tag the effect's pass names — e.g. radl_grss_dsrt_tuft
	 * carries MAIN=7, the pak_* tree LOD cards MAIN=128. 0..255.
	 */
	TMap<FString, uint8> AlphaReferenceValues;

	const FSWGShaderTexture* FindTexture(ESWGShaderTextureUsage Usage) const;
};

/**
 * The fixed-function render states an .eft's passes ask for, which is what
 * decides masked/blended versus opaque — the effect name alone misses
 * a_punchout (alpha test only) and e_radialflora (grass billboards).
 * Read from FORM EFCT > IMPL > PASS > 0009 > DATA, whose layout was pinned
 * down by diffing a_simple/a_alpha/a_punchout/e_radialflora: byte 5 is
 * z-write, byte 7 alpha-blend enable, byte 11 alpha-test enable followed by
 * the 4-byte reference tag (a shader ARVS tag such as MAIN, or a literal
 * "A128"), byte 17 the colour write mask.
 */
struct SWGTRE_API FSWGEffectRenderStates
{
	bool bAlphaBlend = false;
	bool bAlphaTest = false;
	bool bWritesDepth = true;

	/** The ARVS tag the alpha test reads its threshold from (empty when the effect carries a literal). */
	FString AlphaReferenceTag;
	/** A literal threshold from an "Annn" tag; unset when the shader's ARVS supplies it. */
	TOptional<uint8> AlphaReferenceLiteral;

	/** The 0..1 alpha-test threshold for a shader, resolving the tag against its ARVS; Fallback if neither names one. */
	float ResolveAlphaThreshold(const FSWGShaderData& Shader, float Fallback) const;
};

/** Reads the render-relevant parts of FORM SSHT shader templates. */
class SWGTRE_API FSWGShaderReader
{
public:
	static bool ReadShader(const FSWGIffReader& Reader, FSWGShaderData& OutShader);

	/**
	 * Reads an .eft (FORM EFCT). Each implementation's BASE pass is read and
	 * the implementations OR-ed together: a_punchout's shader-model-2 path
	 * clips in the pixel shader and leaves the fixed-function alpha test
	 * off, while its fallback path enables it — the intent is the same
	 * cutout either way. Later passes of an implementation are blended
	 * overlays (dirt, decals) on top of an opaque base and are ignored.
	 */
	static bool ReadEffectRenderStates(const FSWGIffReader& Reader, FSWGEffectRenderStates& OutStates);

private:
	static ESWGShaderTextureUsage ClassifyTextureTag(const FString& Tag);
};
