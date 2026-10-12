#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/** A decoded 8-bit RGBA image (top row first). */
struct SWGTRE_API FSWGImage
{
	int32 Width = 0;
	int32 Height = 0;
	TArray<FColor> Pixels;

	bool IsValid() const { return Width > 0 && Height > 0 && Pixels.Num() == Width * Height; }

	/** Bilinear sample at normalised UV, clamped at the edges; texel centres at (i + 0.5) / size. */
	FLinearColor SampleBilinear(float U, float V) const;
};

/** Top-mip decoder for the DDS flavours the retail TRE ships: DXT1/3/5 and 32-bit uncompressed. */
class SWGTRE_API FSWGDdsDecoder
{
public:
	static bool Decode(const TArray<uint8>& Bytes, FSWGImage& OutImage);
};

/** A customization palette (.pal, little-endian RIFF "PAL data"): entry colours, alpha forced to 255. */
struct SWGTRE_API FSWGPalette
{
	TArray<FColor> Colors;

	static bool Parse(const TArray<uint8>& Bytes, FSWGPalette& OutPalette);
};

/**
 * Retail BlueprintTextureRenderer (.trt, texturerenderer/*.trt). Layout decoded from SWGEmu.exe
 * (loader 0x86eeb0, render command factory 0x86bc20, op factory 0x86dfc0, variable binding 0x873190):
 *
 *   FORM BTRT > FORM 0001
 *     INFO  u32 (1)
 *     DEST  u32 width, u32 height, u32 n, u32[n] texture-format preference list
 *     FORM SHTM  INFO u32 n; n x NAME shader template path (texren_copy / texren_alphablend)
 *     FORM TXTS  INFO u32 n; n x cstring texture path
 *     FORM VBS   INFO u32 n; n x FORM VTXA > FORM 0003 > INFO {u32 0x1101, u32 vertices} DATA {x,y,z,u,v}*
 *     FORM IBS   INFO u32 n (0 in all shipped files)
 *     FORM CAM   PCAM float (parallel projection; 1.0)
 *     FORM RCMS  INFO u32 n; n x FORM SRSC { INFO {u32 shader, u32 prims}, prims x RTFC {u32 vertexBuffer} }
 *                (CFBC chunk: clear-colour flag, ARGB, clear-depth flag, float depth, clear-stencil flag, stencil)
 *     FORM PCMS  INFO u32 n; n x FORM PCMD { [FORM COND] FORM TOPS / FORM FOPS: INFO u32 n, n ops }
 *
 * Ops: SSTC {u32 shader, u32 tag, u32 texture}: fixed texture. SST1 {u32 shader, u32 tag, u32 base,
 * cstring variable, u32 count}: texture = base + clamp(variable, 0, count - 1). STFP {u16 shader, u32
 * tag, cstring palette, cstring variable, u8 private}: texture factor = palette[variable], out of
 * range selecting the last entry. SST2 adds a second variable/count, using row * columnCount + column.
 * STF reads packed ARGB; STFA supplies RGB bytes and variable alpha; NOP is empty.
 * TRUE/FALS and recursive AND/OR conditions select TOPS/FOPS at load. Unknown conditions retain fallback.
 */
struct SWGTRE_API FSWGTextureRenderer
{
	enum class EOpKind : uint8
	{
		SetTexture,
		SetTextureByVariable,
		SetTextureByVariables,
		SetFactorFromPalette,
		SetFactorFromVariable,
		SetFactorAlpha,
		Unsupported,
	};

	struct FOp
	{
		EOpKind Kind = EOpKind::Unsupported;
		int32 ShaderIndex = 0;
		uint32 Tag = 0;
		int32 TextureBase = 0;
		int32 TextureCount = 0;
		int32 SecondTextureCount = 0;
		FString Variable;
		FString SecondVariable;
		FString Palette;
		uint32 FactorRgb = 0xffffff;
	};

	struct FVertex
	{
		float X = 0.f;
		float Y = 0.f;
		float U = 0.f;
		float V = 0.f;
	};

	struct FDraw
	{
		int32 ShaderIndex = 0;
		int32 VertexBuffer = 0;
		bool bClearColor = false;
		uint32 ClearColor = 0;
	};

	int32 Width = 0;
	int32 Height = 0;
	/** Shader templates; those containing "alphablend" blend with SrcAlpha / InvSrcAlpha, the rest overwrite. */
	TArray<FString> Shaders;
	TArray<FString> Textures;
	TArray<TArray<FVertex>> VertexBuffers;
	TArray<FDraw> Draws;
	TArray<FOp> Ops;
	bool bHasUnsupportedOps = false;

	static bool Read(const FSWGIffReader& Reader, FSWGTextureRenderer& OutRenderer);

	bool IsAlphaBlended(int32 ShaderIndex) const;

	/** Every variable the ops read, bare names, no duplicates. */
	TArray<FString> GetVariables() const;
};

/** A .mgn's FORM TRTS entry: a renderer whose output replaces a tagged texture of one of the mesh's shaders. */
struct FSWGMeshTextureRendererRef
{
	FString RendererPath;
	/** PSDT (shader) index within the .mgn, and the shader texture tag (MAIN) that receives the output. */
	TArray<TPair<int32, uint32>> Targets;
};

class SWGTRE_API FSWGMeshTextureRendererReader
{
public:
	/** Reads FORM TRTS > TRT * out of an .mgn; empty when there are none. */
	static TArray<FSWGMeshTextureRendererRef> Read(const FSWGIffReader& MgnReader);

	/** The shader template path of each PSDT in file order (PSDT > NAME). */
	static TArray<FString> ReadShaderNames(const FSWGIffReader& MgnReader);
};

/** CPU composite of a renderer. Replaces the retail GPU render-to-texture pass. */
class SWGTRE_API FSWGTextureCompositor
{
public:
	struct FInputs
	{
		TFunction<const FSWGImage*(const FString&)> LoadImage;
		TFunction<const FSWGPalette*(const FString&)> LoadPalette;
		/** Bare variable name -> value; absent variables read as 0. */
		TFunction<int32(const FString&)> GetVariable;
	};

	static bool Compose(const FSWGTextureRenderer& Renderer, const FInputs& Inputs, FSWGImage& OutImage);

	/** Selects the texture index an op resolves to (SetTexture / SetTextureByVariable), or INDEX_NONE. */
	static int32 ResolveTextureIndex(const FSWGTextureRenderer::FOp& Op, int32 VariableValue, int32 SecondVariableValue = 0);

	/** Palette index an STFP op uses: out of range selects the last entry. */
	static int32 ResolvePaletteIndex(int32 VariableValue, int32 PaletteSize);
};

class UTexture2D;

/** Uploads an FSWGImage as a transient BGRA8 texture with a box-filtered mip chain. */
class SWGTRE_API FSWGImageTexture
{
public:
	static UTexture2D* Create(const FSWGImage& Image, const FName& BaseName, bool bSRGB);
};
