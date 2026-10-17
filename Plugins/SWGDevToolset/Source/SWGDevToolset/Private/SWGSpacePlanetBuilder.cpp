// swg.CreateSpacePlanetAssets - the Stage 4 planet materials. The sun comes in as SunDirection (toward the sun, world space) and
// SunRadiance (light colour * intensity / PI, so it matches Unreal's own Lambert diffuse); the component sets both every Update.

#include "SWGSpacePlanetBuilder.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionPixelNormalWS.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionVectorParameter.h"
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
		UE_LOG(LogTemp, Log, TEXT("swg.CreateSpacePlanetAssets: saved %s"), *Package->GetName());
	}

	UMaterial* CreateOrLoadMaterial(const FString& Name, bool bRebuild)
	{
		const FString PackagePath = TEXT("/Game/SWGEmu/Materials/") + Name;
		if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *Name)))
		{
			if (!bRebuild)
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.CreateSpacePlanetAssets: %s already exists - pass 'rebuild' to regenerate"), *Name);
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

	UMaterialExpressionCustom* AddCustom(UMaterial* Material, int32 PositionX, int32 PositionY, const TCHAR* Description, const FString& Code,
		ECustomMaterialOutputType OutputType, const TArray<FName>& InputNames)
	{
		UMaterialExpressionCustom* Custom = AddExpression<UMaterialExpressionCustom>(Material, PositionX, PositionY);
		Custom->Description = Description; Custom->Code = Code; Custom->OutputType = OutputType;
		Custom->Inputs.Reset();
		for (const FName InputName : InputNames) Custom->Inputs.AddDefaulted_GetRef().InputName = InputName;
		return Custom;
	}

	UMaterialExpressionScalarParameter* AddScalar(UMaterial* Material, const TCHAR* Name, float DefaultValue, int32 PositionX, int32 PositionY)
	{
		auto* Parameter = AddExpression<UMaterialExpressionScalarParameter>(Material, PositionX, PositionY);
		Parameter->ParameterName = Name; Parameter->DefaultValue = DefaultValue;
		return Parameter;
	}

	UMaterialExpressionVectorParameter* AddColor(UMaterial* Material, const TCHAR* Name, const FLinearColor& DefaultValue, int32 PositionX, int32 PositionY)
	{
		auto* Parameter = AddExpression<UMaterialExpressionVectorParameter>(Material, PositionX, PositionY);
		Parameter->ParameterName = Name; Parameter->DefaultValue = DefaultValue;
		return Parameter;
	}

	UMaterialExpressionTextureSampleParameter2D* AddTexture(UMaterial* Material, const TCHAR* Name, int32 PositionX, int32 PositionY)
	{
		auto* Sample = AddExpression<UMaterialExpressionTextureSampleParameter2D>(Material, PositionX, PositionY);
		Sample->ParameterName = Name;
		Sample->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		return Sample;
	}

	/** A texture parameter passed as an object, for a Custom node that samples it itself (HLSL: Name and NameSampler). */
	UMaterialExpressionTextureObjectParameter* AddTextureObject(UMaterial* Material, const TCHAR* Name, int32 PositionX, int32 PositionY)
	{
		auto* Object = AddExpression<UMaterialExpressionTextureObjectParameter>(Material, PositionX, PositionY);
		Object->ParameterName = Name;
		Object->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		return Object;
	}

	UMaterialExpressionConstant* AddConstant(UMaterial* Material, float Value, int32 PositionX, int32 PositionY)
	{
		auto* Constant = AddExpression<UMaterialExpressionConstant>(Material, PositionX, PositionY);
		Constant->R = Value;
		return Constant;
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
				UE_LOG(LogTemp, Error, TEXT("swg.CreateSpacePlanetAssets: %s: %s"), *Material->GetName(), *Error);
				bClean = false;
			}
		SavePackageOf(Material);
		return bClean;
	}

	// Light the engine's hard Lambert term lacks: a Gaussian glow of width Soften centred on the terminator (N.L = 0), 1.6 times as wide on
	// the night side so the light rolls off into the dark like scattered light, not a bright ring (Stage 10).
	// Added as emissive on top of Unreal's own direct lighting, which keeps the fill light and the DTLA/DTLB look untouched.
	const TCHAR* TerminatorWrapCode = TEXT(
		"float3 Up = normalize(Normal);\n"
		"float3 Toward = normalize(Sun);\n"
		"float Facing = dot(Up, Toward);\n"
		"float Spread = max(Soften, 0.001);\n"
		"float Side = Facing < 0.0 ? Spread * 1.6 : Spread;\n"
		"float Wrap = 0.4 * Spread * exp(-(Facing / Side) * (Facing / Side));\n"
		"float Day = smoothstep(-0.2, 0.5, Facing);\n"
		"float Limb = pow(1.0 - saturate(dot(Up, normalize(ViewDir))), LimbPower);\n");

	// The point of the cloud layer that shades this surface point: along the sun ray, LayerHeight above the surface (unit sphere).
	const TCHAR* ShadowDirectionCode = TEXT(
		"float3 Up = normalize(Normal);\n"
		"float3 Toward = normalize(Sun);\n"
		"float Facing = max(dot(Up, Toward), 0.25);\n"
		"return normalize(Up + Toward * (LayerHeight / Facing));\n");

	// Mirrors FSphereGenerator's UVs (U = 1 - azimuth / 2PI, V = colatitude / PI), turned by the cloud's yaw relative to the surface.
	const TCHAR* CloudUvCode = TEXT(
		"float3 Direction = normalize(LocalDirection);\n"
		"float Azimuth = atan2(Direction.y, Direction.x) + YawOffset;\n"
		"float U = 1.0 - Azimuth / 6.28318531;\n"
		"float V = acos(clamp(Direction.z, -1.0, 1.0)) / 3.14159265;\n"
		"return float2(U * RepeatU, V * RepeatV);\n");

	// The globe terrain (Stages 5, 9): a tangent frame at RegionCentre (all in the planet's object space, which the sphere's pole axis
	// is Z in). North is the pole projected onto the tangent plane and East = Centre x North, the handedness Unreal's left-handed
	// local space needs for east to read on the right when the globe is seen from outside, so the baked map appears unmirrored.
	const TCHAR* RegionFrameCode = TEXT(
		"float3 Up = normalize(LocalNormal);\n"
		"float3 Centre = normalize(RegionCentre);\n"
		"float3 Pole = abs(Centre.z) > 0.999 ? float3(1.0, 0.0, 0.0) : float3(0.0, 0.0, 1.0);\n"
		"float3 North = normalize(Pole - Centre * dot(Pole, Centre));\n"
		"float3 East = cross(Centre, North);\n"
		"float Facing = dot(Up, Centre);\n");

	// Shell front-face thickness (shell radius = 1, ground radius = ShellRatio): grazing rays past the ground limb cross both shell
	// sides, rays that reach the ground only the near side. Normalised by the shell depth, so the disc centre is 1.
	const TCHAR* AtmosphereCode = TEXT(
		"float3 Up = normalize(Normal);\n"
		"float3 Back = normalize(ViewDir);\n"
		"float3 Toward = normalize(Sun);\n"
		"float Cosine = saturate(dot(Up, Back));\n"
		"float Chord = Cosine * Cosine - (1.0 - ShellRatio * ShellRatio);\n"
		"float Path = Chord > 0.0 ? Cosine - sqrt(Chord) : 2.0 * Cosine;\n"
		"float AirMass = Path / (1.0 - ShellRatio);\n"
		"float Scatter = 1.0 - exp(-Density * 0.12 * AirMass);\n"
		"float Facing = dot(Up, Toward);\n"
		"float Day = smoothstep(-0.15, 0.55, Facing);\n"
		"float Band = exp(-Facing * Facing / (2.0 * Width * Width));\n"
		"float Forward = pow(saturate(dot(-Back, Toward)), 8.0) * Mie;\n"
		"float3 Rim = Tint * Day * (1.0 + Forward) + TerminatorTint * Band * 0.6 + Tint * NightRim * (1.0 - Day);\n"
		"return Scatter * Rim * SunRadiance;\n");

	void BuildAtmosphere(bool bRebuild)
	{
		UMaterial* Material = CreateOrLoadMaterial(TEXT("M_SWGPlanetAtmosphere"), bRebuild);
		if (!Material) return;
		Material->MaterialDomain = MD_Surface; Material->BlendMode = BLEND_Additive;
		Material->SetShadingModel(MSM_Unlit); Material->TwoSided = false;

		auto* Normal = AddExpression<UMaterialExpressionPixelNormalWS>(Material, -1000, 0);
		auto* ViewDirection = AddExpression<UMaterialExpressionCameraVectorWS>(Material, -1000, 120);
		auto* SunDirection = AddColor(Material, TEXT("SunDirection"), FLinearColor(0, 0, 1), -1000, 240);
		auto* SunRadiance = AddColor(Material, TEXT("SunRadiance"), FLinearColor(1, 1, 1), -1000, 360);
		auto* Tint = AddColor(Material, TEXT("AtmosphereTint"), FLinearColor(.65f, .38f, .16f), -1000, 480);
		auto* TerminatorTint = AddColor(Material, TEXT("TerminatorTint"), FLinearColor(1.f, .45f, .2f), -1000, 600);
		auto* Density = AddScalar(Material, TEXT("Density"), 1.f, -1000, 720);
		auto* NightRim = AddScalar(Material, TEXT("NightRim"), .06f, -1000, 800);
		auto* ShellRatio = AddScalar(Material, TEXT("ShellRatio"), .98f, -1000, 880);
		auto* BandWidth = AddScalar(Material, TEXT("TerminatorWidth"), .16f, -1000, 960);
		auto* Mie = AddScalar(Material, TEXT("ForwardScatter"), 1.5f, -1000, 1040);
		auto* Opacity = AddScalar(Material, TEXT("Opacity"), 1.f, -400, 400);

		auto* Scattering = AddCustom(Material, -600, 400, TEXT("PlanetAtmosphere"), AtmosphereCode, CMOT_Float3,
			{TEXT("Normal"), TEXT("ViewDir"), TEXT("Sun"), TEXT("SunRadiance"), TEXT("Tint"), TEXT("TerminatorTint"), TEXT("Density"),
			TEXT("NightRim"), TEXT("ShellRatio"), TEXT("Width"), TEXT("Mie")});
		Connect(Normal, TEXT(""), Scattering, TEXT("Normal")); Connect(ViewDirection, TEXT(""), Scattering, TEXT("ViewDir"));
		Connect(SunDirection, TEXT("RGB"), Scattering, TEXT("Sun")); Connect(SunRadiance, TEXT("RGB"), Scattering, TEXT("SunRadiance"));
		Connect(Tint, TEXT("RGB"), Scattering, TEXT("Tint")); Connect(TerminatorTint, TEXT("RGB"), Scattering, TEXT("TerminatorTint"));
		Connect(Density, TEXT(""), Scattering, TEXT("Density")); Connect(NightRim, TEXT(""), Scattering, TEXT("NightRim"));
		Connect(ShellRatio, TEXT(""), Scattering, TEXT("ShellRatio")); Connect(BandWidth, TEXT(""), Scattering, TEXT("Width"));
		Connect(Mie, TEXT(""), Scattering, TEXT("Mie"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Scattering, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, TEXT(""), MP_Opacity);
		SaveMaterial(Material);
	}

	void BuildSurface(bool bRebuild)
	{
		UMaterial* Material = CreateOrLoadMaterial(TEXT("M_SWGPlanetSurface"), bRebuild);
		if (!Material) return;
		Material->MaterialDomain = MD_Surface; Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_DefaultLit); Material->TwoSided = false;

		// Retail e_planet_2blend.psh: DTLA at UV0, DTLB at UV0*2, MASK at UV0*0.125; the mask alpha blends DTLB toward DTLA.
		auto* TexCoord = AddExpression<UMaterialExpressionTextureCoordinate>(Material, -1800, 0);
		auto* DetailBCoord = AddExpression<UMaterialExpressionMultiply>(Material, -1600, 120); DetailBCoord->ConstB = 2;
		auto* MaskCoord = AddExpression<UMaterialExpressionMultiply>(Material, -1600, 240); MaskCoord->ConstB = 0.125f;
		auto* DetailA = AddTexture(Material, TEXT("DetailA"), -1400, 0);
		auto* DetailB = AddTexture(Material, TEXT("DetailB"), -1400, 160);
		auto* Mask = AddTexture(Material, TEXT("Mask"), -1400, 320);
		auto* Blend = AddExpression<UMaterialExpressionLinearInterpolate>(Material, -1100, 100);
		Connect(TexCoord, TEXT(""), DetailA, TEXT("UVs"));
		Connect(TexCoord, TEXT(""), DetailBCoord, TEXT("A")); Connect(TexCoord, TEXT(""), MaskCoord, TEXT("A"));
		Connect(DetailBCoord, TEXT(""), DetailB, TEXT("UVs")); Connect(MaskCoord, TEXT(""), Mask, TEXT("UVs"));
		Connect(DetailB, TEXT("RGB"), Blend, TEXT("A")); Connect(DetailA, TEXT("RGB"), Blend, TEXT("B")); Connect(Mask, TEXT("A"), Blend, TEXT("Alpha"));

		auto* Normal = AddExpression<UMaterialExpressionPixelNormalWS>(Material, -1800, 500);
		auto* ViewDirection = AddExpression<UMaterialExpressionCameraVectorWS>(Material, -1800, 620);
		auto* SunDirection = AddColor(Material, TEXT("SunDirection"), FLinearColor(0, 0, 1), -1800, 740);
		auto* SunRadiance = AddColor(Material, TEXT("SunRadiance"), FLinearColor(1, 1, 1), -1800, 860);
		auto* AtmosphereColor = AddColor(Material, TEXT("AtmosphereTint"), FLinearColor(.65f, .38f, .16f), -1800, 980);
		auto* Soften = AddScalar(Material, TEXT("TerminatorSoftness"), .25f, -1800, 1100);
		auto* NightAmbient = AddScalar(Material, TEXT("NightAmbient"), .004f, -1800, 1180);
		auto* AerialStrength = AddScalar(Material, TEXT("AerialStrength"), .35f, -1800, 1260);
		auto* AerialPower = AddScalar(Material, TEXT("AerialPower"), 3.f, -1800, 1340);
		auto* OceanGlint = AddScalar(Material, TEXT("OceanGlint"), 0.f, -1800, 1420);

		// Cloud shadows: the cloud texture is read where the sun ray crosses the cloud layer. LayerHeight is artistic (the real gap,
		// 1/390 of the radius, would put every shadow directly under its cloud). The cloud spin differs from the surface's, so
		// CloudYawOffset (surface yaw minus cloud yaw, radians) turns the lookup into the cloud's frame.
		auto* LayerHeight = AddScalar(Material, TEXT("CloudShadowOffset"), .02f, -1400, 700);
		auto* ShadowStrength = AddScalar(Material, TEXT("CloudShadowStrength"), 0.f, -1400, 780);
		auto* YawOffset = AddScalar(Material, TEXT("CloudYawOffset"), 0.f, -1400, 860);
		auto* RepeatU = AddScalar(Material, TEXT("CloudRepeatU"), 1.f, -1400, 940);
		auto* RepeatV = AddScalar(Material, TEXT("CloudRepeatV"), 1.f, -1400, 1020);
		auto* ShadowDirection = AddCustom(Material, -1100, 700, TEXT("CloudShadowDirection"), ShadowDirectionCode, CMOT_Float3,
			{TEXT("Normal"), TEXT("Sun"), TEXT("LayerHeight")});
		Connect(Normal, TEXT(""), ShadowDirection, TEXT("Normal")); Connect(SunDirection, TEXT("RGB"), ShadowDirection, TEXT("Sun"));
		Connect(LayerHeight, TEXT(""), ShadowDirection, TEXT("LayerHeight"));
		auto* LocalShadowDirection = AddExpression<UMaterialExpressionTransform>(Material, -900, 700);
		LocalShadowDirection->TransformSourceType = TRANSFORMSOURCE_World; LocalShadowDirection->TransformType = TRANSFORM_Local;
		Connect(ShadowDirection, TEXT(""), LocalShadowDirection, TEXT(""));
		auto* CloudCoordinate = AddCustom(Material, -700, 700, TEXT("CloudShadowUV"), CloudUvCode, CMOT_Float2,
			{TEXT("LocalDirection"), TEXT("YawOffset"), TEXT("RepeatU"), TEXT("RepeatV")});
		Connect(LocalShadowDirection, TEXT(""), CloudCoordinate, TEXT("LocalDirection")); Connect(YawOffset, TEXT(""), CloudCoordinate, TEXT("YawOffset"));
		Connect(RepeatU, TEXT(""), CloudCoordinate, TEXT("RepeatU")); Connect(RepeatV, TEXT(""), CloudCoordinate, TEXT("RepeatV"));
		auto* CloudSample = AddTexture(Material, TEXT("CloudTexture"), -450, 700);
		// The UV wraps at the seam; a fixed soft mip keeps the derivative jump from drawing a line there.
		CloudSample->MipValueMode = TMVM_MipLevel; CloudSample->ConstMipValue = 2;
		Connect(CloudCoordinate, TEXT(""), CloudSample, TEXT("UVs"));

		// Globe terrain (Stage 9): two equirectangular maps in the frame at RegionCentre. The cap map (RegionColor / RegionNormal) covers
		// +-RegionCapHalfWidth in longitude and latitude and holds the retail terrain, so it fills the visible cap; the whole-globe map (GlobeFar)
		// carries the generated far side. Both come from one field, so they agree where they overlap and the cap fades into the far map past
		// the visible limb. Alpha of the colour maps is the water classification; alpha of RegionNormal is how real the terrain is.
		// RegionEnabled 0 leaves the generic DTLA/DTLB look untouched.
		auto* RegionCentre = AddColor(Material, TEXT("RegionCentre"), FLinearColor(1, 0, 0), -2600, 1500);
		auto* CapHalfWidth = AddScalar(Material, TEXT("RegionCapHalfWidth"), .5f, -2600, 1620);
		auto* RegionEnabled = AddScalar(Material, TEXT("RegionEnabled"), 0.f, -2600, 1700);
		auto* RegionNormalStrength = AddScalar(Material, TEXT("RegionNormalStrength"), .25f, -2600, 1860);
		auto* RegionAverage = AddColor(Material, TEXT("RegionAverage"), FLinearColor(.5f, .5f, .5f), -2600, 1940);
		auto* DetailAverage = AddColor(Material, TEXT("DetailAverage"), FLinearColor(.5f, .5f, .5f), -2600, 2060);
		auto* RetailVariation = AddScalar(Material, TEXT("RegionRetailVariation"), .12f, -2600, 2180);
		auto* BrightnessMatch = AddScalar(Material, TEXT("RegionBrightnessMatch"), .7f, -2600, 2220);
		auto* VertexNormal = AddExpression<UMaterialExpressionVertexNormalWS>(Material, -2600, 2260);
		auto* LocalNormal = AddExpression<UMaterialExpressionTransform>(Material, -2400, 2260);
		LocalNormal->TransformSourceType = TRANSFORMSOURCE_World; LocalNormal->TransformType = TRANSFORM_Local;
		Connect(VertexNormal, TEXT(""), LocalNormal, TEXT(""));
		auto* GlobeAngles = AddCustom(Material, -2200, 1500, TEXT("PlanetGlobeAngles"),
			FString(RegionFrameCode) + TEXT("return float2(atan2(dot(Up, East), dot(Up, Centre)), asin(clamp(dot(Up, North), -1.0, 1.0)));\n"), CMOT_Float2,
			{TEXT("LocalNormal"), TEXT("RegionCentre")});
		Connect(LocalNormal, TEXT(""), GlobeAngles, TEXT("LocalNormal")); Connect(RegionCentre, TEXT("RGB"), GlobeAngles, TEXT("RegionCentre"));
		auto* CapUv = AddCustom(Material, -2000, 1500, TEXT("PlanetGlobeCapUV"),
			TEXT("return clamp(float2(Angles.x, -Angles.y) / (2.0 * max(Half, 0.01)) + 0.5, 0.0, 1.0);\n"), CMOT_Float2, {TEXT("Angles"), TEXT("Half")});
		Connect(GlobeAngles, TEXT(""), CapUv, TEXT("Angles")); Connect(CapHalfWidth, TEXT(""), CapUv, TEXT("Half"));
		// The cap's square fade starts at the visible limb (the half width is 1.25 times the cap radius).
		auto* CapWeight = AddCustom(Material, -2000, 1700, TEXT("PlanetGlobeCapWeight"),
			TEXT("float Edge = max(abs(Angles.x), abs(Angles.y)) / max(Half, 0.01);\n"
				"return Enabled * (1.0 - smoothstep(0.8, 1.0, Edge));\n"), CMOT_Float1, {TEXT("Angles"), TEXT("Half"), TEXT("Enabled")});
		Connect(GlobeAngles, TEXT(""), CapWeight, TEXT("Angles")); Connect(CapHalfWidth, TEXT(""), CapWeight, TEXT("Half")); Connect(RegionEnabled, TEXT(""), CapWeight, TEXT("Enabled"));
		// The whole-globe map wraps in longitude, so atan2's jump would pick the highest mip along one meridian; the derivatives come from
		// whichever of U and its half-shifted copy is continuous there.
		auto* FarUv = AddCustom(Material, -2000, 1900, TEXT("PlanetGlobeFarUV"),
			TEXT("return float2(Angles.x / 6.28318531 + 0.5, 0.5 - Angles.y / 3.14159265);\n"), CMOT_Float2, {TEXT("Angles")});
		Connect(GlobeAngles, TEXT(""), FarUv, TEXT("Angles"));
		const TCHAR* FarDerivativeCode = TEXT(
			"float U = Angles.x / 6.28318531 + 0.5;\n"
			"float Shifted = frac(U + 0.5);\n"
			"float2 Plain = float2(DERIVATIVE(U), DERIVATIVE(0.5 - Angles.y / 3.14159265));\n"
			"float ShiftedStep = DERIVATIVE(Shifted);\n"
			"Plain.x = abs(Plain.x) > abs(ShiftedStep) ? ShiftedStep : Plain.x;\n"
			"return Plain;\n");
		auto* FarDerivativeX = AddCustom(Material, -2000, 2050, TEXT("PlanetGlobeFarDX"), FString(FarDerivativeCode).Replace(TEXT("DERIVATIVE"), TEXT("ddx")), CMOT_Float2, {TEXT("Angles")});
		auto* FarDerivativeY = AddCustom(Material, -2000, 2150, TEXT("PlanetGlobeFarDY"), FString(FarDerivativeCode).Replace(TEXT("DERIVATIVE"), TEXT("ddy")), CMOT_Float2, {TEXT("Angles")});
		Connect(GlobeAngles, TEXT(""), FarDerivativeX, TEXT("Angles")); Connect(GlobeAngles, TEXT(""), FarDerivativeY, TEXT("Angles"));
		auto* RegionColorSample = AddTexture(Material, TEXT("RegionColor"), -1900, 1500);
		Connect(CapUv, TEXT(""), RegionColorSample, TEXT("UVs"));
		auto* FarColorSample = AddTexture(Material, TEXT("GlobeFar"), -1900, 1900);
		FarColorSample->MipValueMode = TMVM_Derivative;
		Connect(FarUv, TEXT(""), FarColorSample, TEXT("UVs"));
		Connect(FarDerivativeX, TEXT(""), FarColorSample, TEXT("DDX(UVs)")); Connect(FarDerivativeY, TEXT(""), FarColorSample, TEXT("DDY(UVs)"));
		auto* RegionNormalSample = AddTexture(Material, TEXT("RegionNormal"), -1900, 1700);
		// Raw linear texels; the default is a TC_Masks texture so the sampler type validates.
		RegionNormalSample->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineMaterials/DefaultDiffuse_TC_Masks.DefaultDiffuse_TC_Masks"));
		RegionNormalSample->SamplerType = SAMPLERTYPE_Masks;
		Connect(CapUv, TEXT(""), RegionNormalSample, TEXT("UVs"));
		// Ground detail (Stage 10): the retail diffuse textures of the zone's four strongest terrain shader families, weighted by RegionDetail
		// (the bake's per-family weight map, RGBA = families 0..3), tiled at DetailRepeat tiles per radian of the globe. Each is applied as
		// texture / its own mean, so it adds variation and keeps the bake's brightness; a second, turned and rescaled sample hides the tiling.
		// It fades out as a tile shrinks on screen (where the mips would only average it) and over water. DetailStrength 0 is the bake alone.
		auto* RegionDetailSample = AddTexture(Material, TEXT("RegionDetail"), -1900, 2300);
		RegionDetailSample->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineMaterials/DefaultDiffuse_TC_Masks.DefaultDiffuse_TC_Masks"));
		RegionDetailSample->SamplerType = SAMPLERTYPE_Masks;
		Connect(CapUv, TEXT(""), RegionDetailSample, TEXT("UVs"));
		auto* DetailRepeat = AddScalar(Material, TEXT("DetailRepeat"), 400.f, -2600, 2500);
		auto* DetailStrength = AddScalar(Material, TEXT("DetailStrength"), 0.f, -2600, 2580);
		UMaterialExpressionTextureObjectParameter* FamilyTextures[4];
		UMaterialExpressionVectorParameter* FamilyAverages[4];
		const TCHAR* FamilyTextureNames[4] = {TEXT("FamilyTex0"), TEXT("FamilyTex1"), TEXT("FamilyTex2"), TEXT("FamilyTex3")};
		const TCHAR* FamilyAverageNames[4] = {TEXT("FamilyAverage0"), TEXT("FamilyAverage1"), TEXT("FamilyAverage2"), TEXT("FamilyAverage3")};
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			FamilyTextures[Slot] = AddTextureObject(Material, FamilyTextureNames[Slot], -2600, 2700 + Slot*120);
			FamilyAverages[Slot] = AddColor(Material, FamilyAverageNames[Slot], FLinearColor(.5f, .5f, .5f), -2400, 2700 + Slot*120);
		}
		const FString DetailCode = TEXT(
			"float2 Uv = Angles * Repeat;\n"
			"float Footprint = min(max(length(ddx(Uv)), length(ddy(Uv))), 4.0);\n"
			"float Fade = 1.0 - smoothstep(0.05, 0.18, Footprint);\n"
			"float2 Turned = float2(Uv.x * 0.8 + Uv.y * 0.6, Uv.y * 0.8 - Uv.x * 0.6) * 0.37 + float2(0.31, 0.57);\n"
			"float4 Family = float4(Weights, WeightA);\n"
			"float3 Ratio = 0.0;\n"
			"float Covered = 0.0;\n"
			"float3 Texel = 0.5 * (Texture2DSample(Tex0, Tex0Sampler, Uv).rgb + Texture2DSample(Tex0, Tex0Sampler, Turned).rgb);\n"
			"Ratio += Family.x * Texel / max(Avg0, 0.02); Covered += Family.x;\n"
			"Texel = 0.5 * (Texture2DSample(Tex1, Tex1Sampler, Uv).rgb + Texture2DSample(Tex1, Tex1Sampler, Turned).rgb);\n"
			"Ratio += Family.y * Texel / max(Avg1, 0.02); Covered += Family.y;\n"
			"Texel = 0.5 * (Texture2DSample(Tex2, Tex2Sampler, Uv).rgb + Texture2DSample(Tex2, Tex2Sampler, Turned).rgb);\n"
			"Ratio += Family.z * Texel / max(Avg2, 0.02); Covered += Family.z;\n"
			"Texel = 0.5 * (Texture2DSample(Tex3, Tex3Sampler, Uv).rgb + Texture2DSample(Tex3, Tex3Sampler, Turned).rgb);\n"
			"Ratio += Family.w * Texel / max(Avg3, 0.02); Covered += Family.w;\n"
			"Ratio = (Ratio + saturate(1.0 - Covered)) / max(Covered, 1.0);\n"
			"float Lum = dot(Ratio, float3(0.3, 0.59, 0.11));\n"
			"Ratio = clamp(lerp(float3(Lum, Lum, Lum), Ratio, 0.6), 0.35, 2.5);\n"
			"float Amount = Strength * Fade * saturate(CapWeight) * (1.0 - saturate(Water));\n"
			"return lerp(float3(1.0, 1.0, 1.0), Ratio, saturate(Amount));\n");
		auto* DetailRatio = AddCustom(Material, -1600, 2300, TEXT("PlanetGlobeDetail"), DetailCode, CMOT_Float3,
			{TEXT("Angles"), TEXT("Weights"), TEXT("WeightA"), TEXT("Repeat"), TEXT("Strength"), TEXT("CapWeight"), TEXT("Water"),
			TEXT("Tex0"), TEXT("Tex1"), TEXT("Tex2"), TEXT("Tex3"), TEXT("Avg0"), TEXT("Avg1"), TEXT("Avg2"), TEXT("Avg3")});
		Connect(GlobeAngles, TEXT(""), DetailRatio, TEXT("Angles")); Connect(RegionDetailSample, TEXT("RGB"), DetailRatio, TEXT("Weights"));
		Connect(RegionDetailSample, TEXT("A"), DetailRatio, TEXT("WeightA")); Connect(DetailRepeat, TEXT(""), DetailRatio, TEXT("Repeat"));
		Connect(DetailStrength, TEXT(""), DetailRatio, TEXT("Strength")); Connect(CapWeight, TEXT(""), DetailRatio, TEXT("CapWeight"));
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			Connect(FamilyTextures[Slot], TEXT(""), DetailRatio, *FString::Printf(TEXT("Tex%d"), Slot));
			Connect(FamilyAverages[Slot], TEXT("RGB"), DetailRatio, *FString::Printf(TEXT("Avg%d"), Slot));
		}

		// The generic detail only adds close-range variation: its luminance against its own mean, at RegionRetailVariation.
		auto* RegionAlbedo = AddCustom(Material, -1600, 1500, TEXT("PlanetGlobeAlbedo"),
			TEXT("float3 Weights = float3(0.3, 0.59, 0.11);\n"
				"float3 Globe = lerp(Far, Cap, saturate(CapWeight));\n"
				"float DetailMean = max(dot(DetailAverage, Weights), 0.01);\n"
				"float Ratio = clamp(dot(Detail, Weights) / DetailMean, 0.5, 1.8);\n"
				"float Gain = clamp(DetailMean / max(dot(RegionAverage, Weights), 0.01), 1.0, 3.0);\n"
				"float3 Matched = Globe * lerp(1.0, Gain, saturate(Match)) * lerp(1.0, Ratio, saturate(Variation)) * GroundDetail;\n"
				"return lerp(Detail, Matched, saturate(Enabled));\n"), CMOT_Float3,
			{TEXT("Detail"), TEXT("Cap"), TEXT("Far"), TEXT("CapWeight"), TEXT("RegionAverage"), TEXT("DetailAverage"), TEXT("Variation"), TEXT("Enabled"), TEXT("Match"), TEXT("GroundDetail")});
		Connect(DetailRatio, TEXT(""), RegionAlbedo, TEXT("GroundDetail"));
		Connect(Blend, TEXT(""), RegionAlbedo, TEXT("Detail")); Connect(RegionColorSample, TEXT("RGB"), RegionAlbedo, TEXT("Cap"));
		Connect(FarColorSample, TEXT("RGB"), RegionAlbedo, TEXT("Far")); Connect(CapWeight, TEXT(""), RegionAlbedo, TEXT("CapWeight"));
		Connect(RegionAverage, TEXT("RGB"), RegionAlbedo, TEXT("RegionAverage")); Connect(DetailAverage, TEXT("RGB"), RegionAlbedo, TEXT("DetailAverage"));
		Connect(RetailVariation, TEXT(""), RegionAlbedo, TEXT("Variation"));
		Connect(RegionEnabled, TEXT(""), RegionAlbedo, TEXT("Enabled")); Connect(BrightnessMatch, TEXT(""), RegionAlbedo, TEXT("Match"));
		auto* WaterMask = AddCustom(Material, -1600, 1650, TEXT("PlanetGlobeWater"),
			TEXT("return smoothstep(0.25, 0.75, lerp(FarWater, CapWater, saturate(CapWeight)));\n"), CMOT_Float1, {TEXT("CapWater"), TEXT("FarWater"), TEXT("CapWeight")});
		Connect(RegionColorSample, TEXT("A"), WaterMask, TEXT("CapWater")); Connect(FarColorSample, TEXT("A"), WaterMask, TEXT("FarWater"));
		Connect(CapWeight, TEXT(""), WaterMask, TEXT("CapWeight"));
		// The relief normal: tangent-space texel (east, north, up) rebuilt on the frame at this pixel, then turned to world space.
		auto* RegionRelief = AddCustom(Material, -1600, 1800, TEXT("PlanetRegionNormal"),
			FString(RegionFrameCode) + TEXT(
				"float3 Texel = Tangent * 2.0 - 1.0;\n"
				"Texel.xy *= Strength;\n"
				"Texel = normalize(Texel);\n"
				"float3 PixelEast = normalize(East - Up * dot(East, Up));\n"
				"float3 PixelNorth = normalize(North - Up * dot(North, Up));\n"
				"float3 Perturbed = normalize(PixelEast * Texel.x + PixelNorth * Texel.y + Up * Texel.z);\n"
				"return lerp(Up, Perturbed, Mask);\n"), CMOT_Float3,
			{TEXT("LocalNormal"), TEXT("RegionCentre"), TEXT("Tangent"), TEXT("Strength"), TEXT("Mask")});
		Connect(LocalNormal, TEXT(""), RegionRelief, TEXT("LocalNormal")); Connect(RegionCentre, TEXT("RGB"), RegionRelief, TEXT("RegionCentre"));
		Connect(RegionNormalSample, TEXT("RGB"), RegionRelief, TEXT("Tangent")); Connect(RegionNormalStrength, TEXT(""), RegionRelief, TEXT("Strength"));
		Connect(CapWeight, TEXT(""), RegionRelief, TEXT("Mask"));
		auto* WorldRelief = AddExpression<UMaterialExpressionTransform>(Material, -1400, 1800);
		WorldRelief->TransformSourceType = TRANSFORMSOURCE_Local; WorldRelief->TransformType = TRANSFORM_World;
		Connect(RegionRelief, TEXT(""), WorldRelief, TEXT(""));
		auto* WorldReliefUnit = AddExpression<UMaterialExpressionNormalize>(Material, -1250, 1800);
		Connect(WorldRelief, TEXT(""), WorldReliefUnit, TEXT(""));
		Material->bTangentSpaceNormal = false;


		auto* Shaded = AddCustom(Material, -150, 100, TEXT("PlanetCloudShadow"),
			TEXT("float Day = saturate(dot(normalize(Normal), normalize(Sun)));\n"
				"return Albedo * (1.0 - saturate(Strength * CloudAlpha) * smoothstep(0.0, 0.35, Day));\n"), CMOT_Float3,
			{TEXT("Albedo"), TEXT("CloudAlpha"), TEXT("Normal"), TEXT("Sun"), TEXT("Strength")});
		Connect(RegionAlbedo, TEXT(""), Shaded, TEXT("Albedo")); Connect(CloudSample, TEXT("A"), Shaded, TEXT("CloudAlpha"));
		Connect(Normal, TEXT(""), Shaded, TEXT("Normal")); Connect(SunDirection, TEXT("RGB"), Shaded, TEXT("Sun"));
		Connect(ShadowStrength, TEXT(""), Shaded, TEXT("Strength"));

		const FString EmissiveCode = FString(TerminatorWrapCode) + TEXT(
			"float3 Lit = Albedo * SunRadiance * Wrap;\n"
			"float3 Night = Albedo * SunRadiance * NightAmbient * (1.0 - Day);\n"
			"float3 Aerial = AtmosphereColor * SunRadiance * AerialStrength * Limb * Day;\n"
			"return Lit + Night + Aerial;\n");
		auto* Emissive = AddCustom(Material, -150, 500, TEXT("PlanetTerminator"), EmissiveCode, CMOT_Float3,
			{TEXT("Albedo"), TEXT("Normal"), TEXT("ViewDir"), TEXT("Sun"), TEXT("SunRadiance"), TEXT("AtmosphereColor"), TEXT("Soften"),
			TEXT("NightAmbient"), TEXT("AerialStrength"), TEXT("LimbPower")});
		Connect(Shaded, TEXT(""), Emissive, TEXT("Albedo")); Connect(Normal, TEXT(""), Emissive, TEXT("Normal"));
		Connect(ViewDirection, TEXT(""), Emissive, TEXT("ViewDir")); Connect(SunDirection, TEXT("RGB"), Emissive, TEXT("Sun"));
		Connect(SunRadiance, TEXT("RGB"), Emissive, TEXT("SunRadiance")); Connect(AtmosphereColor, TEXT("RGB"), Emissive, TEXT("AtmosphereColor"));
		Connect(Soften, TEXT(""), Emissive, TEXT("Soften")); Connect(NightAmbient, TEXT(""), Emissive, TEXT("NightAmbient"));
		Connect(AerialStrength, TEXT(""), Emissive, TEXT("AerialStrength")); Connect(AerialPower, TEXT(""), Emissive, TEXT("LimbPower"));

		// Sun glint on ocean worlds (OceanGlint is the art table's flag). With a globe the water mask is the bake's own classification;
		// without one, water is dark and bluer than red, which keeps dark land (Corellia's browns, Naboo's forests) matte.
		auto* OceanMask = AddCustom(Material, -350, 900, TEXT("PlanetOceanMask"),
			TEXT("float Luminance = dot(Albedo, float3(0.3, 0.59, 0.11));\n"
				"float Heuristic = (1.0 - smoothstep(0.12, 0.3, Luminance)) * smoothstep(0.01, 0.08, Albedo.b - Albedo.r);\n"
				"return saturate(Glint * lerp(Heuristic, Mask, saturate(Enabled)));\n"), CMOT_Float1, {TEXT("Albedo"), TEXT("Glint"), TEXT("Mask"), TEXT("Enabled")});
		Connect(RegionAlbedo, TEXT(""), OceanMask, TEXT("Albedo")); Connect(OceanGlint, TEXT(""), OceanMask, TEXT("Glint"));
		Connect(WaterMask, TEXT(""), OceanMask, TEXT("Mask")); Connect(RegionEnabled, TEXT(""), OceanMask, TEXT("Enabled"));
		Connect(WaterMask, TEXT(""), DetailRatio, TEXT("Water"));
		// Land is fully rough and non-specular (a smooth, specular land reads as plastic in the sun); only water keeps a glint (Stage 10).
		auto* LandRoughness = AddScalar(Material, TEXT("LandRoughness"), 1.f, -350, 1000);
		auto* LandSpecular = AddScalar(Material, TEXT("LandSpecular"), 0.f, -350, 1080);
		auto* WaterRoughness = AddScalar(Material, TEXT("WaterRoughness"), .12f, -350, 1160);
		auto* WaterSpecular = AddScalar(Material, TEXT("WaterSpecular"), .5f, -350, 1240);
		auto* Roughness = AddCustom(Material, -150, 900, TEXT("PlanetRoughness"), TEXT("return lerp(Land, Water, Mask);\n"), CMOT_Float1, {TEXT("Land"), TEXT("Water"), TEXT("Mask")});
		Connect(LandRoughness, TEXT(""), Roughness, TEXT("Land")); Connect(WaterRoughness, TEXT(""), Roughness, TEXT("Water")); Connect(OceanMask, TEXT(""), Roughness, TEXT("Mask"));
		auto* Specular = AddCustom(Material, -150, 1100, TEXT("PlanetSpecular"), TEXT("return lerp(Land, Water, Mask);\n"), CMOT_Float1, {TEXT("Land"), TEXT("Water"), TEXT("Mask")});
		Connect(LandSpecular, TEXT(""), Specular, TEXT("Land")); Connect(WaterSpecular, TEXT(""), Specular, TEXT("Water")); Connect(OceanMask, TEXT(""), Specular, TEXT("Mask"));

		UMaterialEditingLibrary::ConnectMaterialProperty(Shaded, TEXT(""), MP_BaseColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Emissive, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness);
		UMaterialEditingLibrary::ConnectMaterialProperty(Specular, TEXT(""), MP_Specular);
		UMaterialEditingLibrary::ConnectMaterialProperty(WorldReliefUnit, TEXT(""), MP_Normal);
		SaveMaterial(Material);
	}
	void BuildClouds(bool bRebuild)
	{
		UMaterial* Material = CreateOrLoadMaterial(TEXT("M_SWGPlanetCloud"), bRebuild);
		if (!Material) return;
		Material->MaterialDomain = MD_Surface; Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_DefaultLit); Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting; Material->TwoSided = false;

		auto* Diffuse = AddTexture(Material, TEXT("Diffuse"), -1200, 0);
		auto* Normal = AddExpression<UMaterialExpressionPixelNormalWS>(Material, -1200, 300);
		auto* ViewDirection = AddExpression<UMaterialExpressionCameraVectorWS>(Material, -1200, 420);
		auto* SunDirection = AddColor(Material, TEXT("SunDirection"), FLinearColor(0, 0, 1), -1200, 540);
		auto* SunRadiance = AddColor(Material, TEXT("SunRadiance"), FLinearColor(1, 1, 1), -1200, 660);
		auto* AtmosphereColor = AddColor(Material, TEXT("AtmosphereTint"), FLinearColor(.65f, .38f, .16f), -1200, 780);
		auto* Soften = AddScalar(Material, TEXT("TerminatorSoftness"), .25f, -1200, 900);
		auto* NightAmbient = AddScalar(Material, TEXT("NightAmbient"), .004f, -1200, 980);
		auto* LimbTint = AddScalar(Material, TEXT("LimbTint"), .35f, -1200, 1060);
		auto* LimbSoftness = AddScalar(Material, TEXT("LimbSoftness"), .3f, -1200, 1140);
		auto* LimbPower = AddScalar(Material, TEXT("LimbPower"), 3.f, -1200, 1220);

		// Same terminator wrap as the surface; clouds near the limb take on the atmosphere colour and fade out softly.
		const FString EmissiveCode = FString(TerminatorWrapCode) + TEXT(
			"float3 Lit = Albedo * SunRadiance * Wrap;\n"
			"float3 Night = Albedo * SunRadiance * NightAmbient * (1.0 - Day);\n"
			"float3 Tinted = AtmosphereColor * SunRadiance * LimbTint * Limb * Day;\n"
			"return Lit + Night + Tinted;\n");
		auto* Emissive = AddCustom(Material, -800, 400, TEXT("CloudTerminator"), EmissiveCode, CMOT_Float3,
			{TEXT("Albedo"), TEXT("Normal"), TEXT("ViewDir"), TEXT("Sun"), TEXT("SunRadiance"), TEXT("AtmosphereColor"), TEXT("Soften"),
			TEXT("NightAmbient"), TEXT("LimbTint"), TEXT("LimbPower")});
		Connect(Diffuse, TEXT("RGB"), Emissive, TEXT("Albedo")); Connect(Normal, TEXT(""), Emissive, TEXT("Normal"));
		Connect(ViewDirection, TEXT(""), Emissive, TEXT("ViewDir")); Connect(SunDirection, TEXT("RGB"), Emissive, TEXT("Sun"));
		Connect(SunRadiance, TEXT("RGB"), Emissive, TEXT("SunRadiance")); Connect(AtmosphereColor, TEXT("RGB"), Emissive, TEXT("AtmosphereColor"));
		Connect(Soften, TEXT(""), Emissive, TEXT("Soften")); Connect(NightAmbient, TEXT(""), Emissive, TEXT("NightAmbient"));
		Connect(LimbTint, TEXT(""), Emissive, TEXT("LimbTint")); Connect(LimbPower, TEXT(""), Emissive, TEXT("LimbPower"));

		// Clouds thin out where the viewer looks straight down at the primary planet's globe terrain: ThinStrength 0 leaves them as authored,
		// the cap centre loses that fraction of its opacity, and the effect fades out by ThinRadius (the visible cap) from it.
		auto* ThinDirection = AddColor(Material, TEXT("ThinDirection"), FLinearColor(0, 0, 1), -1200, 1300);
		auto* ThinRadius = AddScalar(Material, TEXT("ThinRadius"), .4f, -1200, 1380);
		auto* ThinStrength = AddScalar(Material, TEXT("ThinStrength"), 0.f, -1200, 1460);
		auto* Opacity = AddCustom(Material, -800, 800, TEXT("CloudLimbFade"),
			TEXT("float Cosine = saturate(dot(normalize(Normal), normalize(ViewDir)));\n"
				"float Angle = acos(clamp(dot(normalize(Normal), normalize(ThinDirection)), -1.0, 1.0));\n"
				"float Thin = 1.0 - ThinStrength * (1.0 - smoothstep(0.3, 1.0, Angle / max(ThinRadius, 0.01)));\n"
				"return Alpha * Thin * smoothstep(0.0, max(Softness, 0.001), Cosine);\n"), CMOT_Float1,
			{TEXT("Alpha"), TEXT("Normal"), TEXT("ViewDir"), TEXT("Softness"), TEXT("ThinDirection"), TEXT("ThinRadius"), TEXT("ThinStrength")});
		Connect(Diffuse, TEXT("A"), Opacity, TEXT("Alpha")); Connect(Normal, TEXT(""), Opacity, TEXT("Normal"));
		Connect(ViewDirection, TEXT(""), Opacity, TEXT("ViewDir")); Connect(LimbSoftness, TEXT(""), Opacity, TEXT("Softness"));
		Connect(ThinDirection, TEXT("RGB"), Opacity, TEXT("ThinDirection")); Connect(ThinRadius, TEXT(""), Opacity, TEXT("ThinRadius"));
		Connect(ThinStrength, TEXT(""), Opacity, TEXT("ThinStrength"));

		// Cloud is a diffuse scatterer: fully rough and non-specular, so the sun leaves no glossy highlight on it (Stage 10).
		auto* CloudRoughness = AddConstant(Material, 1.f, -800, 1000);
		auto* CloudSpecular = AddConstant(Material, 0.f, -800, 1080);

		UMaterialEditingLibrary::ConnectMaterialProperty(Diffuse, TEXT("RGB"), MP_BaseColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Emissive, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, TEXT(""), MP_Opacity);
		UMaterialEditingLibrary::ConnectMaterialProperty(CloudRoughness, TEXT(""), MP_Roughness);
		UMaterialEditingLibrary::ConnectMaterialProperty(CloudSpecular, TEXT(""), MP_Specular);
		SaveMaterial(Material);
	}

	// The retail sun and stars drew a four-point cross texture (shader/cels_star_back, shader/starglow); this replaces it (deliberate departure) with
	// a limb-darkened disc plus a smooth corona, both round by construction. The quad is the retail sprite's, so its UV spans the retail size:
	// DiscRadius and CoronaRadius are fractions of the quad width (0.5 reaches its edge). StarColor and Intensity come from the component each frame.
	const TCHAR* StarCode = TEXT(
		"float Distance = length(Uv - float2(0.5, 0.5));\n"
		"float Edge = max(abs(ddx(Distance)) + abs(ddy(Distance)), 1e-5);\n"
		"float Inside = 1.0 - smoothstep(DiscRadius - Edge, DiscRadius + Edge, Distance);\n"
		"float Mu = sqrt(saturate(1.0 - (Distance / DiscRadius) * (Distance / DiscRadius)));\n"
		"float Limb = 1.0 - Darkening * (1.0 - Mu);\n"
		"float Corona = CoronaStrength / pow(1.0 + (Distance / CoronaRadius) * (Distance / CoronaRadius), 1.5);\n"
		"float Window = 1.0 - smoothstep(0.3, 0.5, Distance);\n"
		"float3 Warm = lerp(float3(1.0, 1.0, 1.0), float3(1.0, 0.82, 0.62), saturate(Distance / (CoronaRadius * 4.0)));\n"
		"return StarColor * (Inside * Limb * DiscIntensity + Corona * DiscIntensity * Window * Warm) * Intensity;\n");

	void BuildSunSprite(bool bRebuild)
	{
		UMaterial* Material = CreateOrLoadMaterial(TEXT("M_SWGSunSprite"), bRebuild);
		if (!Material) return;
		Material->MaterialDomain = MD_Surface; Material->BlendMode = BLEND_Additive;
		// Backgrounds composite before the gas volumes, as the other sky sprites do.
		Material->TranslucencyPass = MTP_BeforeDOF;
		Material->SetShadingModel(MSM_Unlit); Material->TwoSided = true;

		auto* TexCoord = AddExpression<UMaterialExpressionTextureCoordinate>(Material, -900, 0);
		auto* DiscRadius = AddScalar(Material, TEXT("DiscRadius"), .018f, -900, 120);
		auto* CoronaRadius = AddScalar(Material, TEXT("CoronaRadius"), .05f, -900, 200);
		auto* CoronaStrength = AddScalar(Material, TEXT("CoronaStrength"), .45f, -900, 280);
		auto* Darkening = AddScalar(Material, TEXT("LimbDarkening"), .55f, -900, 360);
		auto* DiscIntensity = AddScalar(Material, TEXT("DiscIntensity"), 12.f, -900, 440);
		auto* Intensity = AddScalar(Material, TEXT("Intensity"), 1.f, -900, 520);
		auto* StarColor = AddColor(Material, TEXT("StarColor"), FLinearColor::White, -900, 600);
		auto* Star = AddCustom(Material, -500, 200, TEXT("SunDiscAndCorona"), StarCode, CMOT_Float3,
			{TEXT("Uv"), TEXT("DiscRadius"), TEXT("CoronaRadius"), TEXT("CoronaStrength"), TEXT("Darkening"), TEXT("DiscIntensity"), TEXT("Intensity"), TEXT("StarColor")});
		Connect(TexCoord, TEXT(""), Star, TEXT("Uv")); Connect(DiscRadius, TEXT(""), Star, TEXT("DiscRadius")); Connect(CoronaRadius, TEXT(""), Star, TEXT("CoronaRadius"));
		Connect(CoronaStrength, TEXT(""), Star, TEXT("CoronaStrength")); Connect(Darkening, TEXT(""), Star, TEXT("Darkening"));
		Connect(DiscIntensity, TEXT(""), Star, TEXT("DiscIntensity")); Connect(Intensity, TEXT(""), Star, TEXT("Intensity")); Connect(StarColor, TEXT("RGB"), Star, TEXT("StarColor"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Star, TEXT(""), MP_EmissiveColor);
		SaveMaterial(Material);
	}
}

namespace SWGSpacePlanetAssets
{
	void BuildAll(bool bRebuild)
	{
		BuildAtmosphere(bRebuild);
		BuildSurface(bRebuild);
		BuildClouds(bRebuild);
		BuildSunSprite(bRebuild);
	}
}
