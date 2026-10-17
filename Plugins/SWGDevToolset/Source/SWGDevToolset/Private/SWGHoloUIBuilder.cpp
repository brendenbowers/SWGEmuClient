// swg.CreateHoloUIAssets - the screen-space hologram look of the piloting HUD. Two additive UI materials share one look
// (scanlines, a slow vertical sweep, flicker, grain, a chromatic edge, a pulse and a wipe-in reveal) and differ in where the
// shape comes from: M_SWGHoloUI cuts a sprite out of an atlas, M_SWGHoloUIArc draws a gauge arc analytically.

#include "SWGHoloUIBuilder.h"
#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionPixelNormalWS.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "AssetToolsModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

namespace
{
	void SavePackageOf(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Asset, *FileName, Args);
		UE_LOG(LogTemp, Log, TEXT("swg.CreateHoloUIAssets: saved %s"), *Package->GetName());
	}

	UMaterial* CreateOrLoadMaterial(const FString& Name, bool bRebuild)
	{
		const FString PackagePath = TEXT("/Game/SWGEmu/Materials/") + Name;
		if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *Name)))
		{
			if (!bRebuild)
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.CreateHoloUIAssets: %s already exists - pass 'rebuild' to regenerate"), *Name);
				return nullptr;
			}
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(Existing);
			return Existing;
		}
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		return Cast<UMaterial>(AssetTools.CreateAsset(Name, TEXT("/Game/SWGEmu/Materials"), UMaterial::StaticClass(), Factory));
	}

	template <typename T>
	T* AddExpression(UMaterial* Material, int32 PositionX, int32 PositionY)
	{
		return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(Material, T::StaticClass(), PositionX, PositionY));
	}

	void Connect(UMaterialExpression* From, const TCHAR* OutputName, UMaterialExpression* To, const TCHAR* InputName)
	{
		UMaterialEditingLibrary::ConnectMaterialExpressions(From, OutputName, To, InputName);
	}

	bool SaveMaterial(UMaterial* Material)
	{
		UMaterialEditingLibrary::RecompileMaterial(Material);
		bool bClean = true;
		if (const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
			for (const FString& Error : Resource->GetCompileErrors())
			{
				UE_LOG(LogTemp, Error, TEXT("swg.CreateHoloUIAssets: %s: %s"), *Material->GetName(), *Error);
				bClean = false;
			}
		SavePackageOf(Material);
		return bClean;
	}

	struct FScalarInput { const TCHAR* Name; float Default; };
	struct FColorInput { const TCHAR* Name; FLinearColor Default; };

	// The holo look every HUD material shares. These scalars are the MID parameters the widget drives.
	const FScalarInput LookScalars[] = {
		{TEXT("Opacity"), 1.f}, {TEXT("Pulse"), 0.f}, {TEXT("PulseGain"), 1.4f}, {TEXT("Reveal"), 1.f}, {TEXT("HoloStrength"), 1.f},
		{TEXT("ScanRows"), 360.f}, {TEXT("ScanDepth"), 0.28f}, {TEXT("SweepSpeed"), 0.11f}, {TEXT("SweepWidth"), 0.035f},
		{TEXT("SweepGain"), 0.55f}, {TEXT("FlickerAmount"), 0.22f}, {TEXT("FlickerRate"), 11.f}, {TEXT("NoiseAmount"), 0.18f},
		{TEXT("NoiseCells"), 90.f}, {TEXT("Seed"), 0.f}, {TEXT("RegionYOffset"), 0.f}, {TEXT("RegionYSpan"), 1.f},
		{TEXT("ChromaShift"), 0.006f}};

	// Reads AlphaRGB (per-channel coverage, offset for the chromatic edge) and BaseColor, writes the emissive and opacity.
	// ScreenY is the element's position on the whole HUD so scanlines and the sweep line up across elements; Reveal wipes
	// the element in from the top behind a bright leading edge. A rare flicker step dims everything for a frame or two.
	const TCHAR* HoloLookCode = TEXT(
		"float ScreenY = RegionYOffset + UV.y * RegionYSpan;\n"
		"float Scan = 1.0 - ScanDepth * (0.5 + 0.5 * sin(ScreenY * ScanRows * 6.2831853));\n"
		"float SweepPosition = frac(TimeSeconds * SweepSpeed + Seed * 0.37);\n"
		"float SweepDistance = abs(ScreenY - SweepPosition);\n"
		"SweepDistance = min(SweepDistance, 1.0 - SweepDistance);\n"
		"float Sweep = SweepGain * exp(-pow(SweepDistance / max(SweepWidth, 0.0001), 2.0));\n"
		"float FlickerStep = floor(TimeSeconds * FlickerRate + Seed * 17.0);\n"
		"float FlickerRandom = frac(sin(FlickerStep * 12.9898 + Seed * 78.233) * 43758.5453);\n"
		"float Flicker = 1.0 - FlickerAmount * (FlickerRandom > 0.9 ? 1.0 : 0.15 * FlickerRandom);\n"
		"float Grain = frac(sin(dot(floor(UV * NoiseCells) + FlickerStep, float2(12.9898, 78.233))) * 43758.5453);\n"
		"float Look = lerp(1.0, Scan * Flicker * (1.0 + NoiseAmount * (Grain - 0.5)), HoloStrength) + Sweep * HoloStrength;\n"
		"float WipeFront = Reveal * 1.2 - 0.1;\n"
		"float WipeMask = saturate((WipeFront - UV.y) / 0.08 + 0.5);\n"
		"float WipeEdge = Reveal < 1.0 ? exp(-pow((UV.y - WipeFront) / 0.035, 2.0)) : 0.0;\n"
		"float3 Emissive = (BaseColor * (Look + Pulse * PulseGain) + WipeEdge * float3(0.55, 0.9, 1.0)) * AlphaRGB * WipeMask * Opacity * VertexRGB * VertexAlpha;\n"
		"float CoverageAlpha = max(AlphaRGB.r, max(AlphaRGB.g, AlphaRGB.b));\n"
		"return float4(Emissive, saturate(CoverageAlpha * WipeMask * Opacity * VertexAlpha));\n");

	// Atlas sprite: U0..U1 and V0..V1 map the widget's 0..1 onto the sprite's rectangle in the atlas (a reversed pair mirrors it).
	// Solid draws a plain bar instead: lit up to FillX along the widget, with a dim track after (the status panel's condition bars).
	const TCHAR* SpriteShapeCode = TEXT(
		"float2 RectMin = float2(min(U0, U1), min(V0, V1));\n"
		"float2 RectMax = float2(max(U0, U1), max(V0, V1));\n"
		"float2 SpriteUV = float2(lerp(U0, U1, UV.x), lerp(V0, V1, UV.y));\n"
		"float2 Shift = float2((U1 - U0) * ChromaShift, 0.0);\n"
		"float3 AlphaRGB;\n"
		"AlphaRGB.r = Texture2DSample(Sprite, SpriteSampler, clamp(SpriteUV + Shift, RectMin, RectMax)).a;\n"
		"AlphaRGB.g = Texture2DSample(Sprite, SpriteSampler, clamp(SpriteUV, RectMin, RectMax)).a;\n"
		"AlphaRGB.b = Texture2DSample(Sprite, SpriteSampler, clamp(SpriteUV - Shift, RectMin, RectMax)).a;\n"
		"float BarMask = UV.x <= FillX ? 1.0 : TrackAlpha;\n"
		"AlphaRGB = lerp(AlphaRGB, float3(BarMask, BarMask, BarMask), Solid);\n"
		"float3 BaseColor = Tint;\n");

	// Gauge arc: angles are turns, 0 east and growing clockwise on screen. The ring runs from the outer edge in by Thickness
	// (a fraction of the radius) and fades from InnerOpacity to full outward, as the retail Ellipse's inner radius colour does.
	// Fill lights the arc up to a fraction along it, the rest is the dim track; Tick and the overdrive region sit on top.
	// The chromatic edge shifts the radius per channel.
	const TCHAR* ArcShapeCode = TEXT(
		"float2 Centered = (UV - 0.5) * 2.0;\n"
		"float Radius = length(Centered);\n"
		"float Angle = atan2(Centered.y, Centered.x) * 0.15915494;\n"
		"float Span = ArcEnd - ArcBegin;\n"
		"float Direction = Span < 0.0 ? -1.0 : 1.0;\n"
		"float SpanLength = max(abs(Span), 0.0001);\n"
		"float Delta = frac(Direction * (Angle - ArcBegin) + 0.0005) - 0.0005;\n"
		"float Along = Delta / SpanLength;\n"
		"float Inside = (1.0 - smoothstep(SpanLength - 0.002, SpanLength + 0.002, Delta)) * smoothstep(-0.002, 0.002, Delta);\n"
		"float Filled = 1.0 - smoothstep(Fill - 0.004, Fill + 0.004, Along);\n"
		"float TickMask = TickAt >= 0.0 ? 1.0 - smoothstep(TickWidth * 0.5, TickWidth * 0.5 + 0.004, abs(Along - TickAt) * SpanLength) : 0.0;\n"
		"float OverdriveMask = Along >= OverdriveFrom ? 1.0 : 0.0;\n"
		"float3 AlphaRGB = float3(0, 0, 0);\n"
		"float3 BaseColor = OuterColor;\n"
		"for (int Channel = 0; Channel < 3; ++Channel)\n"
		"{\n"
		"	float RingPosition = (Radius + (Channel - 1) * ChromaShift - (1.0 - Thickness)) / max(Thickness, 0.0001);\n"
		"	float RingMask = smoothstep(0.0, 0.05, RingPosition) * (1.0 - smoothstep(0.95, 1.0, RingPosition));\n"
		"	float Gradient = lerp(InnerOpacity, 1.0, saturate(RingPosition));\n"
		"	float Level = lerp(TrackOpacity, 1.0, Filled) * Gradient;\n"
		"	AlphaRGB[Channel] = RingMask * Inside * max(Level, TickMask * RingMask);\n"
		"	if (Channel == 1)\n"
		"	{\n"
		"		BaseColor = lerp(InnerColor, OuterColor, saturate(RingPosition));\n"
		"		BaseColor = lerp(BaseColor, OverdriveColor, OverdriveMask);\n"
		"		BaseColor = lerp(BaseColor, TickColor, TickMask);\n"
		"	}\n"
		"}\n");

	void BuildHoloMaterial(UMaterial* Material, const TCHAR* ShapeCode, const TArray<FScalarInput>& ShapeScalars,
		const TArray<FColorInput>& ShapeColors, bool bSprite)
	{
		Material->MaterialDomain = MD_UI; Material->BlendMode = BLEND_Additive;

		auto* Custom = AddExpression<UMaterialExpressionCustom>(Material, 0, 0);
		Custom->Description = TEXT("HoloHud"); Custom->OutputType = CMOT_Float4;
		Custom->Code = FString(ShapeCode) + HoloLookCode;
		Custom->Inputs.Reset();
		int32 PositionY = 0;
		// Inputs are named first, then wired by name, as the other builders do.
		auto AddInput = [&](const TCHAR* InputName, UMaterialExpression* Source, const TCHAR* OutputName)
		{
			Custom->Inputs.AddDefaulted_GetRef().InputName = InputName;
			Connect(Source, OutputName, Custom, InputName);
			PositionY += 90;
		};
		auto AddScalarInput = [&](const FScalarInput& Entry)
		{
			auto* Parameter = AddExpression<UMaterialExpressionScalarParameter>(Material, -600, PositionY);
			Parameter->ParameterName = Entry.Name; Parameter->DefaultValue = Entry.Default;
			AddInput(Entry.Name, Parameter, TEXT(""));
		};
		auto AddColorInput = [&](const FColorInput& Entry)
		{
			auto* Parameter = AddExpression<UMaterialExpressionVectorParameter>(Material, -600, PositionY);
			Parameter->ParameterName = Entry.Name; Parameter->DefaultValue = Entry.Default;
			AddInput(Entry.Name, Parameter, TEXT("RGB"));
		};

		AddInput(TEXT("UV"), AddExpression<UMaterialExpressionTextureCoordinate>(Material, -600, PositionY), TEXT(""));
		AddInput(TEXT("TimeSeconds"), AddExpression<UMaterialExpressionTime>(Material, -600, PositionY), TEXT(""));
		auto* VertexColor = AddExpression<UMaterialExpressionVertexColor>(Material, -600, PositionY);
		AddInput(TEXT("VertexRGB"), VertexColor, TEXT(""));
		AddInput(TEXT("VertexAlpha"), VertexColor, TEXT("A"));
		for (const FScalarInput& Entry : LookScalars) AddScalarInput(Entry);
		for (const FScalarInput& Entry : ShapeScalars) AddScalarInput(Entry);
		for (const FColorInput& Entry : ShapeColors) AddColorInput(Entry);
		if (bSprite)
		{
			auto* SpriteTexture = AddExpression<UMaterialExpressionTextureObjectParameter>(Material, -600, PositionY);
			SpriteTexture->ParameterName = TEXT("Sprite");
			SpriteTexture->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
			AddInput(TEXT("Sprite"), SpriteTexture, TEXT(""));
		}

		auto* Rgb = AddExpression<UMaterialExpressionComponentMask>(Material, 400, -50);
		Rgb->R = true; Rgb->G = true; Rgb->B = true; Rgb->A = false;
		auto* Alpha = AddExpression<UMaterialExpressionComponentMask>(Material, 400, 80);
		Alpha->R = false; Alpha->G = false; Alpha->B = false; Alpha->A = true;
		Connect(Custom, TEXT(""), Rgb, TEXT("")); Connect(Custom, TEXT(""), Alpha, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialProperty(Rgb, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Alpha, TEXT(""), MP_Opacity);
	}
}

namespace
{
	// The 3D radar's mesh material. Mode 0 draws a latitude/longitude cage with a bright equator on the engine sphere, mode 1 the range
	// rings, spokes and a forward spoke on a plane (forward is UV v = 0), mode 2 a solid glowing marker. Unlit and additive, with the depth
	// test off so the cockpit never hides it; the Rim input is a fresnel from the pixel normal and camera vector.
	const TCHAR* RadarShapeCode = TEXT(
		"float3 UnitNormal = normalize(Normal);\n"
		"float3 UnitView = normalize(CameraVector);\n"
		"float Fresnel = pow(1.0 - saturate(abs(dot(UnitNormal, UnitView))), 2.5);\n"
		"float Band = exp(-pow((frac(TimeSeconds * 0.18 + Seed) - UV.y) / 0.05, 2.0));\n"
		"float FlickerRandom = frac(sin(floor(TimeSeconds * 9.0 + Seed * 13.0) * 12.9898) * 43758.5453);\n"
		"float Flicker = 1.0 - 0.12 * step(0.93, FlickerRandom);\n"
		"float Level = 0.0;\n"
		"if (Mode < 0.5)\n"
		"{\n"
		"	float2 Grid = UV * float2(12.0, 6.0);\n"
		"	float2 LineDistance = abs(frac(Grid - 0.5) - 0.5);\n"
		"	float2 LineWidth = max(fwidth(Grid) * 1.1, 0.0001);\n"
		"	float Cage = max(1.0 - saturate(LineDistance.x / LineWidth.x), 1.0 - saturate(LineDistance.y / LineWidth.y));\n"
		"	float Equator = 1.0 - saturate(abs(UV.y - 0.5) / max(fwidth(UV.y) * 2.0, 0.0001));\n"
		"	Level = Cage * 0.28 + Fresnel * 0.5 + Equator * 0.55 + Band * 0.25;\n"
		"}\n"
		"else if (Mode < 1.5)\n"
		"{\n"
		"	float2 Centered = (UV - 0.5) * 2.0;\n"
		"	float Radius = length(Centered);\n"
		"	float RingWidth = max(fwidth(Radius) * 1.3, 0.0001);\n"
		"	float Rings = 1.0 - saturate(abs(Radius - 1.0) / RingWidth);\n"
		"	Rings = max(Rings, 0.7 * (1.0 - saturate(abs(Radius - 0.6667) / RingWidth)));\n"
		"	Rings = max(Rings, 0.5 * (1.0 - saturate(abs(Radius - 0.3333) / RingWidth)));\n"
		"	float SpokeCoordinate = atan2(Centered.y, Centered.x) * 1.9098593;\n"
		"	float Spokes = (1.0 - saturate(abs(frac(SpokeCoordinate + 0.5) - 0.5) / max(fwidth(SpokeCoordinate) * 1.2, 0.0001))) * smoothstep(0.1, 0.25, Radius);\n"
		"	float ForwardSpoke = Centered.y < 0.0 ? 1.0 - saturate(abs(Centered.x) / max(fwidth(Centered.x) * 2.0, 0.0001)) : 0.0;\n"
		"	float Inside = Radius <= 1.0 + RingWidth ? 1.0 : 0.0;\n"
		"	Level = (Rings * 0.9 + Spokes * 0.18 + ForwardSpoke * 0.8 + (0.05 + 0.08 * Band) * (1.0 - Radius)) * Inside;\n"
		"}\n"
		"else\n"
		"{\n"
		"	Level = 0.5 + 0.5 * Fresnel + 0.1 * Band;\n"
		"}\n"
		"return float4(Color * Intensity * Level * Flicker * Reveal, Reveal);\n");

	void BuildRadarMaterial(UMaterial* Material)
	{
		Material->MaterialDomain = MD_Surface; Material->BlendMode = BLEND_Additive;
		Material->SetShadingModel(MSM_Unlit); Material->TwoSided = true; Material->bDisableDepthTest = true;
		Material->SetMaterialUsage(MATUSAGE_InstancedStaticMeshes);

		auto* Custom = AddExpression<UMaterialExpressionCustom>(Material, 0, 0);
		Custom->Description = TEXT("HoloRadar"); Custom->OutputType = CMOT_Float4;
		Custom->Code = RadarShapeCode;
		Custom->Inputs.Reset();
		int32 PositionY = 0;
		auto AddInput = [&](const TCHAR* InputName, UMaterialExpression* Source, const TCHAR* OutputName)
		{
			Custom->Inputs.AddDefaulted_GetRef().InputName = InputName;
			Connect(Source, OutputName, Custom, InputName);
			PositionY += 90;
		};
		AddInput(TEXT("UV"), AddExpression<UMaterialExpressionTextureCoordinate>(Material, -600, PositionY), TEXT(""));
		AddInput(TEXT("TimeSeconds"), AddExpression<UMaterialExpressionTime>(Material, -600, PositionY), TEXT(""));
		AddInput(TEXT("Normal"), AddExpression<UMaterialExpressionPixelNormalWS>(Material, -600, PositionY), TEXT(""));
		AddInput(TEXT("CameraVector"), AddExpression<UMaterialExpressionCameraVectorWS>(Material, -600, PositionY), TEXT(""));
		for (const FScalarInput& Entry : {FScalarInput{TEXT("Mode"), 2.f}, FScalarInput{TEXT("Intensity"), 1.f}, FScalarInput{TEXT("Reveal"), 1.f}, FScalarInput{TEXT("Seed"), 0.f}})
		{
			auto* Parameter = AddExpression<UMaterialExpressionScalarParameter>(Material, -600, PositionY);
			Parameter->ParameterName = Entry.Name; Parameter->DefaultValue = Entry.Default;
			AddInput(Entry.Name, Parameter, TEXT(""));
		}
		auto* ColorParameter = AddExpression<UMaterialExpressionVectorParameter>(Material, -600, PositionY);
		ColorParameter->ParameterName = TEXT("Color"); ColorParameter->DefaultValue = FLinearColor(0.59f, 0.96f, 0.99f);
		AddInput(TEXT("Color"), ColorParameter, TEXT("RGB"));

		auto* Rgb = AddExpression<UMaterialExpressionComponentMask>(Material, 400, -50);
		Rgb->R = true; Rgb->G = true; Rgb->B = true; Rgb->A = false;
		auto* Alpha = AddExpression<UMaterialExpressionComponentMask>(Material, 400, 80);
		Alpha->R = false; Alpha->G = false; Alpha->B = false; Alpha->A = true;
		Connect(Custom, TEXT(""), Rgb, TEXT("")); Connect(Custom, TEXT(""), Alpha, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialProperty(Rgb, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Alpha, TEXT(""), MP_Opacity);
	}

	// M_SWGHologram drawn over the cockpit: the HUD's target hologram uses it, other holograms keep the depth-tested original.
	void BuildHologramHud()
	{
		const TCHAR* Name = TEXT("M_SWGHologramHud");
		UMaterial* Hud = LoadObject<UMaterial>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGHologramHud.M_SWGHologramHud"));
		if (!Hud)
		{
			UMaterial* Source = LoadObject<UMaterial>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGHologram.M_SWGHologram"));
			if (!Source) { UE_LOG(LogTemp, Warning, TEXT("swg.CreateHoloUIAssets: M_SWGHologram is missing, so M_SWGHologramHud was not made")); return; }
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			Hud = Cast<UMaterial>(AssetTools.DuplicateAsset(Name, TEXT("/Game/SWGEmu/Materials"), Source));
			if (!Hud) return;
		}
		Hud->bDisableDepthTest = true;
		SaveMaterial(Hud);
	}
}

namespace SWGHoloUIAssets
{
	void BuildAll(bool bRebuild)
	{
		BuildHologramHud();
		if (UMaterial* Radar = CreateOrLoadMaterial(TEXT("M_SWGHoloRadar"), bRebuild))
		{
			BuildRadarMaterial(Radar);
			SaveMaterial(Radar);
		}
		if (UMaterial* Sprite = CreateOrLoadMaterial(TEXT("M_SWGHoloUI"), bRebuild))
		{
			BuildHoloMaterial(Sprite, SpriteShapeCode, {{TEXT("U0"), 0.f}, {TEXT("V0"), 0.f}, {TEXT("U1"), 1.f}, {TEXT("V1"), 1.f}, {TEXT("Solid"), 0.f}, {TEXT("FillX"), 1.f}, {TEXT("TrackAlpha"), 0.2f}},
				{{TEXT("Tint"), FLinearColor(0.59f, 0.96f, 0.99f)}}, true);
			SaveMaterial(Sprite);
		}
		if (UMaterial* Arc = CreateOrLoadMaterial(TEXT("M_SWGHoloUIArc"), bRebuild))
		{
			BuildHoloMaterial(Arc, ArcShapeCode,
				{{TEXT("ArcBegin"), 0.3f}, {TEXT("ArcEnd"), 0.7f}, {TEXT("Fill"), 1.f}, {TEXT("Thickness"), 0.12f}, {TEXT("InnerOpacity"), 0.4f},
				 {TEXT("TrackOpacity"), 0.16f}, {TEXT("TickAt"), -1.f}, {TEXT("TickWidth"), 0.012f}, {TEXT("OverdriveFrom"), 2.f}},
				{{TEXT("OuterColor"), FLinearColor(0.14f, 0.87f, 1.f)}, {TEXT("InnerColor"), FLinearColor(0.18f, 0.92f, 1.f)},
				 {TEXT("TickColor"), FLinearColor(1.f, 0.67f, 0.f)}, {TEXT("OverdriveColor"), FLinearColor(1.f, 0.f, 0.f)}}, false);
			SaveMaterial(Arc);
		}
	}
}
