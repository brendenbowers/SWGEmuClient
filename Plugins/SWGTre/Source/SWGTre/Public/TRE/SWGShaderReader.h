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

/**
 * FORM TSNS (43 of 11780 SSHT shaders; water, waterfalls, clouds, monitor text): one 20-byte record =
 * reversed texture tag + 4 floats. Confirmed by vertex_program/a_scroll.vsh (`uv0 = uv + textureScroll.xy`) and
 * a_scroll_rgb1_a2.vsh (second UV set uses .zw): the floats are UV offset rates, so offset = rate * time in seconds
 * (the per-second scaling is inferred: the water code at SWGEmu.exe 0x854010 sets -wind*scale as a constant that only makes sense as a
 * rate; the time application lives in the Direct3D9 DLL, not the exe).
 */
struct FSWGShaderScroll
{
	FString Tag;
	float U1 = 0.f;
	float V1 = 0.f;
	float U2 = 0.f;
	float V2 = 0.f;
};

struct SWGTRE_API FSWGShaderData
{
	TArray<FSWGShaderTexture> Textures;

	/** TSNS UV scroll records (empty for 99.6% of shaders). */
	TArray<FSWGShaderScroll> Scrolls;

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

	/** True when the shader carries its own FORM EFCT instead of naming a .eft (e.g. pt_flash_no_z_sort.sht, EffectName is then empty); FSWGShaderReader::ReadEffect on the same reader reads it. */
	bool bHasInlineEffect = false;

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

/** How a SWTS/SWSH animation chooses its frame. Chunk layouts confirmed from the 50 retail animated shaders. */
enum class ESWGShaderTiming : uint8
{
	None,
	/** DTST 0000: int32 frame count, float seconds per frame (anim_mission_terminal = 8 frames at 0.1 s). */
	DeltaTime,
	/** DFST 0000: int32 frame count only; the frame is set by code (2 sample shaders). */
	DeltaFrame,
	/** DRFS 0000: int32 frame count; a random frame is picked (2 sample shaders). */
	RandomFrame,
	/** DRTS 0000 (0xb5a270, runtime 0xb5ada0/0xb5aeb0): frame count + two floats = min/max hold seconds (either order); each hold picks a RANDOM frame in [0,count). */
	RandomTime
};

struct FSWGShaderAnimationTexture
{
	/** Texture slot, un-reversed ("MAIN"). */
	FString Tag;
	FString TexturePath;
};

/**
 * FORM SWTS (texture flipbook: 39 files) or FORM SWSH (shader switch: 7 files, e.g. lightsaberblade.sht = 4 colour shaders at 1/16 s).
 *   SWTS/0000: NAME base shader, timing form, TEXT x N ([reversed tag][path]).
 *   SWSH/0000: timing form, NAME x N (the shaders to cycle).
 */
struct SWGTRE_API FSWGShaderAnimation
{
	bool bSwitchesShaders = false;
	ESWGShaderTiming Timing = ESWGShaderTiming::None;
	int32 FrameCount = 0;
	/** DTST only. */
	float SecondsPerFrame = 0.f;
	/** DRTS only. */
	float RandomTimeA = 0.f;
	float RandomTimeB = 0.f;
	/** SWTS only. */
	FString BaseShaderPath;
	TArray<FSWGShaderAnimationTexture> Textures;
	/** SWSH only. */
	TArray<FString> Shaders;

	int32 GetDeclaredFrames() const { return bSwitchesShaders ? Shaders.Num() : Textures.Num(); }

	/** The frame DeltaTime shows at TimeSeconds, looping; 0 for the other timings. */
	int32 GetFrameAt(float TimeSeconds) const;
};

/** Pixel-pipeline names read from a PASS; blend factor ids confirmed by ui_multiply (Zero, SrcColor) and the alpha/additive effects. */
enum class ESWGEffectBlendFactor : uint8
{
	Zero, One, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DestAlpha, InvDestAlpha, DestColor, InvDestColor, SrcAlphaSat
};

/** What a particle/mesh pass needs from the material: the answer to "additive or alpha". */
enum class ESWGEffectBlendMode : uint8
{
	Opaque,
	/** Alpha test, no blending. */
	Masked,
	/** SrcAlpha / InvSrcAlpha. */
	Translucent,
	/** Destination factor One (e_particle_emisadd, a_emisadd, lightsaber blade). */
	Additive,
	/** Reverse-subtract (e_particle_subtract). */
	Subtractive,
	/** Multiplies the framebuffer (ui_multiply, ui_overlay, ui_subtract). */
	Modulate,
	Other
};

/**
 * One FORM PASS > FORM 0000..0009. DATA (56 bytes in 0009, 51 in 0008, 47 in 0007) starts with an 18-byte state block whose
 * layout is confirmed for versions 5-9 by diffing a_simple/a_alpha/a_punchout/e_particle_*; later bytes are kept raw.
 * Versions 0 and 2 (font.eft, defaulteffect.eft) use an older shifted layout: only RawData and the shader paths are filled.
 */
struct SWGTRE_API FSWGEffectPass
{
	int32 Version = 0;
	/** DATA[0]: number of fixed-function texture stages (PFFP + STAG forms); 0 when a vertex/pixel program is used. */
	uint8 FixedFunctionStages = 0;
	/** DATA[2]: 0, 1 (set on every additive pass) or 2 (inferred: fog colour mode). */
	uint8 FogMode = 0;
	bool bDepthTest = false;
	bool bDepthWrite = false;
	/** DATA[6]: 3 everywhere (a comparison enum, presumably less-equal). */
	uint8 DepthCompare = 0;
	bool bAlphaBlend = false;
	/** DATA[8]: 0 = add, 2 = reverse-subtract (e_particle_subtract), 4 once (font). */
	uint8 BlendOperation = 0;
	ESWGEffectBlendFactor SourceFactor = ESWGEffectBlendFactor::One;
	ESWGEffectBlendFactor DestFactor = ESWGEffectBlendFactor::Zero;
	bool bAlphaTest = false;
	/** DATA[12..15], un-reversed: an ARVS tag (MAIN) or "Annn". */
	FString AlphaReferenceTag;
	/** DATA[16]: 4 everywhere. */
	uint8 AlphaCompare = 0;
	/** DATA[17]: 0xF = RGBA, 7 = RGB, 8 = alpha only. */
	uint8 ColorWriteMask = 0;

	/** PVSH / PPSH program paths (empty for fixed-function passes). */
	FString VertexProgram;
	FString PixelProgram;
	TArray<uint8> RawData;

	ESWGEffectBlendMode GetBlendMode() const;
};

/** One IMPL form: a shader-capability tier with its passes. Tiers are listed best first. */
struct FSWGEffectImplementation
{
	int32 Version = 0;
	/** IMPL DATA[0]: 0..5 (quality/ lighting-model index; unknown). */
	uint8 Level = 0;
	/** IMPL DATA[1..4] un-reversed: the render phase - ALPH, MAIN, ENV, PREA, PSTA, WBLW, WATR, BLOB. */
	FString Phase;
	/** IMPL DATA[5..6] (v5+): two flag bytes (0/1); unknown. */
	uint8 Flags[2] = {};
	/** SCAP (4/8/12/20 bytes) and OPTN (4/8 bytes), verbatim. */
	TArray<uint8> Capabilities;
	TArray<uint8> Options;
	TArray<FSWGEffectPass> Passes;
};

/** Everything a .eft (FORM EFCT) says. 245 retail files, 20 distinct pass blend setups. */
struct SWGTRE_API FSWGEffectData
{
	int32 Version = 0;
	TArray<FSWGEffectImplementation> Implementations;

	/** Blend mode of the first listed implementation's first pass (Opaque if there is none). */
	ESWGEffectBlendMode GetBlendMode() const;
};

/** Reads the render-relevant parts of FORM SSHT shader templates. */
class SWGTRE_API FSWGShaderReader
{
public:
	static bool ReadShader(const FSWGIffReader& Reader, FSWGShaderData& OutShader);

	/** Reads a FORM SWTS / FORM SWSH animated shader. */
	static bool ReadAnimation(const FSWGIffReader& Reader, FSWGShaderAnimation& OutAnimation);

	/** Reads every implementation and pass of an .eft. */
	static bool ReadEffect(const FSWGIffReader& Reader, FSWGEffectData& OutEffect);

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
