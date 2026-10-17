// swg.CreateSpaceDustAssets - M_SWGSpaceDust, the material of the Stage 3 dust quads. Each quad is a camera-facing capsule
// stretched in world-position offset along the ship's frame-relative velocity.

#include "SWGSpaceDustBuilder.h"
#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionPerInstanceRandom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
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
		UE_LOG(LogTemp, Log, TEXT("swg.CreateSpaceDustAssets: saved %s"), *Package->GetName());
	}

	UMaterial* CreateOrLoadMaterial(const FString& Name, bool bRebuild)
	{
		const FString PackagePath = TEXT("/Game/SWGEmu/Materials/") + Name;
		if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *Name)))
		{
			if (!bRebuild)
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.CreateSpaceDustAssets: %s already exists - pass 'rebuild' to regenerate"), *Name);
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
				UE_LOG(LogTemp, Error, TEXT("swg.CreateSpaceDustAssets: %s: %s"), *Material->GetName(), *Error);
				bClean = false;
			}
		SavePackageOf(Material);
		return bClean;
	}

	// Shared by the vertex and pixel code so both agree on the quad axes. Translated world space is camera-relative, so the
	// direction to the instance is its position. The streak is projected into the view plane: a streak along the line of sight
	// foreshortens to a dot instead of collapsing the quad.
	const TCHAR* StreakFrameCode = TEXT(
		"float3 ViewDir = normalize(InstancePosition);\n"
		"float3 StreakWorld = Streak * Boost;\n"
		"float StreakLength = length(StreakWorld);\n"
		"float3 Axis = StreakLength > 0.001 ? StreakWorld / StreakLength : float3(0, 0, 1);\n"
		"float3 Flat = Axis - ViewDir * dot(Axis, ViewDir);\n"
		"float FlatLength = length(Flat);\n"
		"float3 Fallback = cross(ViewDir, float3(0, 0, 1));\n"
		"float FallbackLength = length(Fallback);\n"
		"Fallback = FallbackLength > 0.001 ? Fallback / FallbackLength : float3(0, 1, 0);\n"
		"float3 Along = FlatLength > 0.001 ? Flat / FlatLength : Fallback;\n"
		"float ScreenLength = StreakLength * FlatLength;\n");

	// The quad runs from half a dot behind the instance position to half a dot past the end of its streak. The instance scale
	// is 1, or 0 for dust hidden inside the hull, and multiplies the size so a hidden quad collapses.
	const TCHAR* BillboardCode = TEXT(
		"float Scale = length(VertexPosition - InstancePosition) * 1.41421356 / 100.0;\n"
		"float Width = DotSize * Scale;\n"
		"float QuadLength = ScreenLength * Scale + Width;\n"
		"float3 Side = cross(ViewDir, Along);\n"
		"float3 Corner = InstancePosition - Along * (Width * 0.5) + Along * (QuadLength * UV.x) + Side * ((UV.y - 0.5) * Width);\n"
		"return Corner - VertexPosition;\n");

	// A capsule around the streak segment, softened to the edge. Long streaks dim so energy per pixel stays steady. The radial
	// fade is the retail one: 128 minus the truncated squared-distance fraction times 128, normalized to the peak.
	const TCHAR* AlphaCode = TEXT(
		"float Width = DotSize;\n"
		"float AlongPosition = UV.x * (ScreenLength + Width) - Width * 0.5;\n"
		"float Across = (UV.y - 0.5) * Width;\n"
		"float Outside = max(max(-AlongPosition, AlongPosition - ScreenLength), 0.0);\n"
		"float Edge = saturate(1.0 - length(float2(Outside, Across)) / (Width * 0.5));\n"
		"float Soft = Edge * Edge * (3.0 - 2.0 * Edge);\n"
		"Soft *= max(0.35, 1.0 / (1.0 + ScreenLength / (DotSize * 6.0)));\n"
		"float Range = length(InstancePosition);\n"
		"float Radial = (128.0 - floor(128.0 * saturate(Range * Range / (DustRadius * DustRadius)))) / 128.0;\n"
		"float NearFade = smoothstep(80.0, 300.0, Range);\n"
		"float Glint = pow(saturate(dot(ViewDir, -LightDirection)), 8.0) * GlintStrength;\n"
		"return saturate(Soft * Radial * NearFade * Opacity * (0.6 + 0.4 * Random) * (1.0 + Glint));\n");
}

namespace SWGSpaceDustAssets
{
	void BuildAll(bool bRebuild)
	{
		UMaterial* Material = CreateOrLoadMaterial(TEXT("M_SWGSpaceDust"), bRebuild);
		if (!Material) return;
		Material->MaterialDomain = MD_Surface; Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_Unlit); Material->TwoSided = true;
		Material->TranslucencyPass = MTP_BeforeDOF;
		Material->SetMaterialUsage(MATUSAGE_InstancedStaticMeshes);

		auto* TexCoord = AddExpression<UMaterialExpressionTextureCoordinate>(Material, -1400, 0);
		auto* Origin = AddExpression<UMaterialExpressionConstant3Vector>(Material, -1400, 150); Origin->Constant = FLinearColor::Black;
		auto* InstancePosition = AddExpression<UMaterialExpressionTransformPosition>(Material, -1200, 150);
		// Local space on an instanced mesh is the component's; instance space gives each quad its own centre.
		InstancePosition->TransformSourceType = TRANSFORMPOSSOURCE_Instance; InstancePosition->TransformType = TRANSFORMPOSSOURCE_TranslatedWorld;
		Connect(Origin, TEXT(""), InstancePosition, TEXT(""));
		auto* VertexPosition = AddExpression<UMaterialExpressionWorldPosition>(Material, -1200, 300);
		VertexPosition->WorldPositionShaderOffset = WPT_CameraRelativeNoOffsets;
		auto* Random = AddExpression<UMaterialExpressionPerInstanceRandom>(Material, -1200, 450);

		auto* StreakVector = AddColor(Material, TEXT("StreakVector"), FLinearColor::Black, -1400, 600);
		auto* StreakBoost = AddScalar(Material, TEXT("StreakBoost"), 1.f, -1400, 750);
		auto* DotSize = AddScalar(Material, TEXT("DotSize"), 10.f, -1400, 850);
		auto* DustRadius = AddScalar(Material, TEXT("DustRadius"), 6400.f, -1400, 950);
		auto* Opacity = AddScalar(Material, TEXT("Opacity"), .8f, -1400, 1050);
		auto* LightDirection = AddColor(Material, TEXT("LightDirection"), FLinearColor(0, 0, -1), -1400, 1150);
		auto* GlintStrength = AddScalar(Material, TEXT("GlintStrength"), 2.f, -1400, 1300);

		const FString BillboardSource = FString(StreakFrameCode) + BillboardCode;
		auto* Billboard = AddCustom(Material, -800, 150, TEXT("StreakBillboard"), BillboardSource, CMOT_Float3,
			{TEXT("UV"), TEXT("InstancePosition"), TEXT("VertexPosition"), TEXT("Streak"), TEXT("Boost"), TEXT("DotSize")});
		Connect(TexCoord, TEXT(""), Billboard, TEXT("UV")); Connect(InstancePosition, TEXT(""), Billboard, TEXT("InstancePosition"));
		Connect(VertexPosition, TEXT(""), Billboard, TEXT("VertexPosition")); Connect(StreakVector, TEXT("RGB"), Billboard, TEXT("Streak"));
		Connect(StreakBoost, TEXT(""), Billboard, TEXT("Boost")); Connect(DotSize, TEXT(""), Billboard, TEXT("DotSize"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Billboard, TEXT(""), MP_WorldPositionOffset);

		const FString AlphaSource = FString(StreakFrameCode) + AlphaCode;
		auto* Alpha = AddCustom(Material, -800, 600, TEXT("DustAlpha"), AlphaSource, CMOT_Float1,
			{TEXT("UV"), TEXT("InstancePosition"), TEXT("Streak"), TEXT("Boost"), TEXT("DotSize"), TEXT("DustRadius"), TEXT("Opacity"), TEXT("LightDirection"), TEXT("GlintStrength"), TEXT("Random")});
		Connect(TexCoord, TEXT(""), Alpha, TEXT("UV")); Connect(InstancePosition, TEXT(""), Alpha, TEXT("InstancePosition"));
		Connect(StreakVector, TEXT("RGB"), Alpha, TEXT("Streak")); Connect(StreakBoost, TEXT(""), Alpha, TEXT("Boost"));
		Connect(DotSize, TEXT(""), Alpha, TEXT("DotSize")); Connect(DustRadius, TEXT(""), Alpha, TEXT("DustRadius"));
		Connect(Opacity, TEXT(""), Alpha, TEXT("Opacity")); Connect(LightDirection, TEXT("RGB"), Alpha, TEXT("LightDirection"));
		Connect(GlintStrength, TEXT(""), Alpha, TEXT("GlintStrength")); Connect(Random, TEXT(""), Alpha, TEXT("Random"));
		// Keeps dust from slicing into the hull or other opaque geometry.
		auto* DepthFade = AddExpression<UMaterialExpressionDepthFade>(Material, -400, 600); DepthFade->FadeDistanceDefault = 50.f;
		Connect(Alpha, TEXT(""), DepthFade, TEXT(""));

		auto* Tint = AddColor(Material, TEXT("Tint"), FLinearColor(.85f, .9f, 1.f), -800, 0);
		auto* Glow = AddScalar(Material, TEXT("Glow"), 1.f, -800, -100);
		auto* Emissive = AddExpression<UMaterialExpressionMultiply>(Material, -400, 0);
		Connect(Tint, TEXT("RGB"), Emissive, TEXT("A")); Connect(Glow, TEXT(""), Emissive, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Emissive, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(DepthFade, TEXT(""), MP_Opacity);
		SaveMaterial(Material);
	}
}
