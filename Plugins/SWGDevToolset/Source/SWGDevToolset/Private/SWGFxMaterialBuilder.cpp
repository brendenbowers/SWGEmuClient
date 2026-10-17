// swg.CreateFxMaterials - builds the particle masters SWGEffects instances per shader. Per-instance custom data
// (layout shared with USWGParticleEffectComponent): 0-3 colour RGBA, 4 flipbook frame, 5-6 width/length in cm,
// 7 rotation radians, 8-10 align axis (zero = face the camera).

#include "SWGFxMaterialBuilder.h"
#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "AssetToolsModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Engine/Texture2D.h"
#include "ShaderCompiler.h"

namespace
{
	enum class EFxVariant : uint8 { Additive, Translucent, Masked, Modulate, Subtractive };

	/** Billboard in WPO. Everything is translated-world (camera at the origin) so no large-world values reach HLSL. */
	const TCHAR* BillboardCode = TEXT(
		"float2 Quad = float2(UV.x - 0.5, 0.5 - UV.y);\n"
		"float3 Forward = normalize(InstancePosition);\n"
		"float3 Axis = float3(AxisX, AxisY, AxisZ);\n"
		"float3 Right;\n"
		"float3 Up;\n"
		"if (dot(Axis, Axis) > 0.25)\n"
		"{\n"
		"    Up = normalize(Axis);\n"
		"    Right = cross(Up, Forward);\n"
		"    float RightLength = length(Right);\n"
		"    Right = RightLength > 0.001 ? Right / RightLength : float3(0, 1, 0);\n"
		"}\n"
		"else\n"
		"{\n"
		"    Right = cross(float3(0, 0, 1), Forward);\n"
		"    float RightLength = length(Right);\n"
		"    Right = RightLength > 0.001 ? Right / RightLength : float3(0, 1, 0);\n"
		"    Up = cross(Forward, Right);\n"
		"    float RollSin;\n"
		"    float RollCos;\n"
		"    sincos(Rotation, RollSin, RollCos);\n"
		"    float3 RolledRight = Right * RollCos + Up * RollSin;\n"
		"    Up = Up * RollCos - Right * RollSin;\n"
		"    Right = RolledRight;\n"
		"}\n"
		"float3 Desired = InstancePosition + Right * (Quad.x * Width) + Up * (Quad.y * Length);\n"
		"return Desired - VertexPosition;\n");

	const TCHAR* SubUvCode = TEXT(
		"float Cells = max(Columns, 1.0);\n"
		"float Index = floor(Frame + 0.5);\n"
		"float Column = fmod(Index, Cells);\n"
		"float Row = floor(Index / Cells);\n"
		"return (UV + float2(Column, Row)) / Cells;\n");

	template <typename T>
	T* AddExpression(UMaterial* Material, int32 PositionX, int32 PositionY)
	{
		return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(Material, T::StaticClass(), PositionX, PositionY));
	}

	UMaterial* CreateOrLoadMaterial(const FString& Name, bool bRebuildExisting)
	{
		const FString PackagePath = TEXT("/Game/SWGEmu/Materials/") + Name;
		if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *Name)))
		{
			if (!bRebuildExisting)
			{
				UE_LOG(LogTemp, Warning, TEXT("swg.CreateFxMaterials: %s already exists - pass 'rebuild' to regenerate"), *Name);
				return nullptr;
			}
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(Existing);
			return Existing;
		}
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		return Cast<UMaterial>(AssetTools.CreateAsset(Name, TEXT("/Game/SWGEmu/Materials"), UMaterial::StaticClass(), Factory));
	}

	UTexture2D* DefaultParticleTexture()
	{
		if (UTexture2D* White = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")))
		{
			return White;
		}
		return LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	}

	UMaterialExpressionCustom* AddCustom(UMaterial* Material, int32 PositionX, int32 PositionY, const TCHAR* Description, const TCHAR* Code,
		ECustomMaterialOutputType OutputType, const TArray<FName>& InputNames)
	{
		UMaterialExpressionCustom* Custom = AddExpression<UMaterialExpressionCustom>(Material, PositionX, PositionY);
		Custom->Description = Description;
		Custom->Code = Code;
		Custom->OutputType = OutputType;
		Custom->Inputs.Reset();
		for (const FName InputName : InputNames)
		{
			Custom->Inputs.AddDefaulted_GetRef().InputName = InputName;
		}
		return Custom;
	}

	UMaterialExpressionPerInstanceCustomData* AddCustomData(UMaterial* Material, int32 DataIndex, float DefaultValue, int32 PositionY)
	{
		auto* Data = AddExpression<UMaterialExpressionPerInstanceCustomData>(Material, -1400, PositionY);
		Data->DataIndex = DataIndex;
		Data->ConstDefaultValue = DefaultValue;
		return Data;
	}

	void SaveMaterial(UMaterial* Material)
	{
		UMaterialEditingLibrary::RecompileMaterial(Material);
		if (const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
		{
			for (const FString& Error : Resource->GetCompileErrors())
			{
				UE_LOG(LogTemp, Error, TEXT("swg.CreateFxMaterials: %s: %s"), *Material->GetName(), *Error);
			}
		}
		UPackage* Package = Material->GetOutermost();
		const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Material, *FileName, Args);
		UE_LOG(LogTemp, Log, TEXT("swg.CreateFxMaterials: saved %s"), *Package->GetName());
	}

	void BuildVariant(const FString& Name, EFxVariant Variant, bool bRebuildExisting)
	{
		UE_LOG(LogTemp, Log, TEXT("swg.CreateFxMaterials: building %s"), *Name);
		UMaterial* Material = CreateOrLoadMaterial(Name, bRebuildExisting);
		if (!Material)
		{
			return;
		}

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		Material->SetMaterialUsage(MATUSAGE_InstancedStaticMeshes);
		switch (Variant)
		{
			case EFxVariant::Additive: Material->BlendMode = BLEND_Additive; break;
			case EFxVariant::Translucent: Material->BlendMode = BLEND_Translucent; break;
			case EFxVariant::Masked: Material->BlendMode = BLEND_Masked; break;
			case EFxVariant::Modulate:
			case EFxVariant::Subtractive: Material->BlendMode = BLEND_Modulate; break;
		}

		UE_LOG(LogTemp, Log, TEXT("swg.CreateFxMaterials: asset created, adding nodes"));
		// Vertex stage: camera-facing or axis-aligned quad.
		auto* TexCoord = AddExpression<UMaterialExpressionTextureCoordinate>(Material, -1400, -300);
		TexCoord->CoordinateIndex = 0;
		auto* Origin = AddExpression<UMaterialExpressionConstant3Vector>(Material, -1400, -150);
		Origin->Constant = FLinearColor(0.f, 0.f, 0.f);
		auto* InstancePosition = AddExpression<UMaterialExpressionTransformPosition>(Material, -1200, -150);
		InstancePosition->TransformSourceType = TRANSFORMPOSSOURCE_Local;
		InstancePosition->TransformType = TRANSFORMPOSSOURCE_TranslatedWorld;
		auto* VertexPosition = AddExpression<UMaterialExpressionWorldPosition>(Material, -1200, -50);
		VertexPosition->WorldPositionShaderOffset = WPT_CameraRelativeNoOffsets;

		auto* ColorR = AddCustomData(Material, 0, 1.f, 0);
		auto* ColorG = AddCustomData(Material, 1, 1.f, 80);
		auto* ColorB = AddCustomData(Material, 2, 1.f, 160);
		auto* ColorA = AddCustomData(Material, 3, 1.f, 240);
		auto* Frame = AddCustomData(Material, 4, 0.f, 320);
		auto* Width = AddCustomData(Material, 5, 100.f, 400);
		auto* Length = AddCustomData(Material, 6, 100.f, 480);
		auto* Rotation = AddCustomData(Material, 7, 0.f, 560);
		auto* AxisX = AddCustomData(Material, 8, 0.f, 640);
		auto* AxisY = AddCustomData(Material, 9, 0.f, 720);
		auto* AxisZ = AddCustomData(Material, 10, 0.f, 800);

		UMaterialExpressionCustom* Billboard = AddCustom(Material, -900, 0, TEXT("Billboard"), BillboardCode, CMOT_Float3,
			{TEXT("UV"), TEXT("InstancePosition"), TEXT("VertexPosition"), TEXT("Width"), TEXT("Length"), TEXT("Rotation"), TEXT("AxisX"), TEXT("AxisY"), TEXT("AxisZ")});
		UMaterialEditingLibrary::ConnectMaterialExpressions(Origin, TEXT(""), InstancePosition, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialExpressions(TexCoord, TEXT(""), Billboard, TEXT("UV"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(InstancePosition, TEXT(""), Billboard, TEXT("InstancePosition"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(VertexPosition, TEXT(""), Billboard, TEXT("VertexPosition"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Width, TEXT(""), Billboard, TEXT("Width"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Length, TEXT(""), Billboard, TEXT("Length"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Rotation, TEXT(""), Billboard, TEXT("Rotation"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(AxisX, TEXT(""), Billboard, TEXT("AxisX"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(AxisY, TEXT(""), Billboard, TEXT("AxisY"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(AxisZ, TEXT(""), Billboard, TEXT("AxisZ"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Billboard, TEXT(""), MP_WorldPositionOffset);

		// Pixel stage: flipbook cell of the shader's texture, tinted by the instance colour.
		auto* Columns = AddExpression<UMaterialExpressionScalarParameter>(Material, -900, 300);
		Columns->ParameterName = TEXT("GridColumns");
		Columns->DefaultValue = 1.f;
		UMaterialExpressionCustom* SubUv = AddCustom(Material, -650, 150, TEXT("SubUV"), SubUvCode, CMOT_Float2, {TEXT("UV"), TEXT("Frame"), TEXT("Columns")});
		UMaterialEditingLibrary::ConnectMaterialExpressions(TexCoord, TEXT(""), SubUv, TEXT("UV"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Frame, TEXT(""), SubUv, TEXT("Frame"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Columns, TEXT(""), SubUv, TEXT("Columns"));

		auto* Texture = AddExpression<UMaterialExpressionTextureSampleParameter2D>(Material, -400, 150);
		Texture->ParameterName = TEXT("Texture");
		Texture->Texture = DefaultParticleTexture();
		UMaterialEditingLibrary::ConnectMaterialExpressions(SubUv, TEXT(""), Texture, TEXT("UVs"));

		auto* ColorRG = AddExpression<UMaterialExpressionAppendVector>(Material, -650, 400);
		auto* ColorRGB = AddExpression<UMaterialExpressionAppendVector>(Material, -500, 400);
		UMaterialEditingLibrary::ConnectMaterialExpressions(ColorR, TEXT(""), ColorRG, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(ColorG, TEXT(""), ColorRG, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(ColorRG, TEXT(""), ColorRGB, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(ColorB, TEXT(""), ColorRGB, TEXT("B"));

		auto* Tinted = AddExpression<UMaterialExpressionMultiply>(Material, -200, 200);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Texture, TEXT("RGB"), Tinted, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(ColorRGB, TEXT(""), Tinted, TEXT("B"));

		auto* TextureAlpha = AddExpression<UMaterialExpressionMultiply>(Material, -200, 350);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Texture, TEXT("A"), TextureAlpha, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(ColorA, TEXT(""), TextureAlpha, TEXT("B"));

		switch (Variant)
		{
			case EFxVariant::Additive:
				// SWG additive ignores the texture alpha; the particle alpha scales the glow.
				UMaterialEditingLibrary::ConnectMaterialProperty(Tinted, TEXT(""), MP_EmissiveColor);
				UMaterialEditingLibrary::ConnectMaterialProperty(ColorA, TEXT(""), MP_Opacity);
				break;
			case EFxVariant::Translucent:
				UMaterialEditingLibrary::ConnectMaterialProperty(Tinted, TEXT(""), MP_EmissiveColor);
				UMaterialEditingLibrary::ConnectMaterialProperty(TextureAlpha, TEXT(""), MP_Opacity);
				break;
			case EFxVariant::Masked:
				Material->OpacityMaskClipValue = 0.3f;
				UMaterialEditingLibrary::ConnectMaterialProperty(Tinted, TEXT(""), MP_EmissiveColor);
				UMaterialEditingLibrary::ConnectMaterialProperty(TextureAlpha, TEXT(""), MP_OpacityMask);
				break;
			case EFxVariant::Modulate:
			{
				// Multiplies the scene by the texture, fading toward 1 (no change) as the particle alpha falls.
				auto* White = AddExpression<UMaterialExpressionConstant3Vector>(Material, -200, 500);
				White->Constant = FLinearColor::White;
				auto* Fade = AddExpression<UMaterialExpressionLinearInterpolate>(Material, 0, 300);
				UMaterialEditingLibrary::ConnectMaterialExpressions(White, TEXT(""), Fade, TEXT("A"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(Tinted, TEXT(""), Fade, TEXT("B"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(ColorA, TEXT(""), Fade, TEXT("Alpha"));
				UMaterialEditingLibrary::ConnectMaterialProperty(Fade, TEXT(""), MP_EmissiveColor);
				break;
			}
			case EFxVariant::Subtractive:
			{
				// Best effort: there is no reverse-subtract blend, so darken the scene by (1 - colour x alpha).
				auto* Strength = AddExpression<UMaterialExpressionMultiply>(Material, 0, 300);
				UMaterialEditingLibrary::ConnectMaterialExpressions(Tinted, TEXT(""), Strength, TEXT("A"));
				UMaterialEditingLibrary::ConnectMaterialExpressions(ColorA, TEXT(""), Strength, TEXT("B"));
				auto* Clamped = AddExpression<UMaterialExpressionSaturate>(Material, 150, 300);
				UMaterialEditingLibrary::ConnectMaterialExpressions(Strength, TEXT(""), Clamped, TEXT(""));
				auto* Darken = AddExpression<UMaterialExpressionOneMinus>(Material, 300, 300);
				UMaterialEditingLibrary::ConnectMaterialExpressions(Clamped, TEXT(""), Darken, TEXT(""));
				UMaterialEditingLibrary::ConnectMaterialProperty(Darken, TEXT(""), MP_EmissiveColor);
				break;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("swg.CreateFxMaterials: graph wired, recompiling"));
		SaveMaterial(Material);
	}
}

// Ribbon and blade masters for dynamic-mesh geometry: texture x Tint, faded by UV1.x (the geometry's own fade channel).
static void BuildRibbonVariant(const FString& Name, bool bAdditive, bool bRebuildExisting)
{
	UE_LOG(LogTemp, Log, TEXT("swg.CreateFxMaterials: building %s"), *Name);
	UMaterial* Material = CreateOrLoadMaterial(Name, bRebuildExisting);
	if (!Material)
	{
		return;
	}

	Material->MaterialDomain = MD_Surface;
	Material->SetShadingModel(MSM_Unlit);
	Material->TwoSided = true;
	Material->BlendMode = bAdditive ? BLEND_Additive : BLEND_Translucent;

	auto* TexCoord = AddExpression<UMaterialExpressionTextureCoordinate>(Material, -800, 0);
	TexCoord->CoordinateIndex = 0;
	auto* Texture = AddExpression<UMaterialExpressionTextureSampleParameter2D>(Material, -500, 0);
	Texture->ParameterName = TEXT("Texture");
	Texture->Texture = DefaultParticleTexture();
	UMaterialEditingLibrary::ConnectMaterialExpressions(TexCoord, TEXT(""), Texture, TEXT("UVs"));

	auto* Tint = AddExpression<UMaterialExpressionVectorParameter>(Material, -500, 250);
	Tint->ParameterName = TEXT("Tint");
	Tint->DefaultValue = FLinearColor::White;

	auto* Colored = AddExpression<UMaterialExpressionMultiply>(Material, -200, 100);
	UMaterialEditingLibrary::ConnectMaterialExpressions(Texture, TEXT("RGB"), Colored, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Tint, TEXT("RGB"), Colored, TEXT("B"));
	UMaterialEditingLibrary::ConnectMaterialProperty(Colored, TEXT(""), MP_EmissiveColor);

	auto* FadeCoord = AddExpression<UMaterialExpressionTextureCoordinate>(Material, -800, 450);
	FadeCoord->CoordinateIndex = 1;
	auto* Fade = AddExpression<UMaterialExpressionComponentMask>(Material, -550, 450);
	Fade->R = true;
	Fade->G = false;
	Fade->B = false;
	Fade->A = false;
	UMaterialEditingLibrary::ConnectMaterialExpressions(FadeCoord, TEXT(""), Fade, TEXT(""));

	auto* TintAlpha = AddExpression<UMaterialExpressionMultiply>(Material, -200, 400);
	UMaterialEditingLibrary::ConnectMaterialExpressions(Fade, TEXT(""), TintAlpha, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Tint, TEXT("A"), TintAlpha, TEXT("B"));
	if (bAdditive)
	{
		// SWG additive ignores the texture alpha.
		UMaterialEditingLibrary::ConnectMaterialProperty(TintAlpha, TEXT(""), MP_Opacity);
	}
	else
	{
		auto* WithTextureAlpha = AddExpression<UMaterialExpressionMultiply>(Material, 0, 400);
		UMaterialEditingLibrary::ConnectMaterialExpressions(TintAlpha, TEXT(""), WithTextureAlpha, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Texture, TEXT("A"), WithTextureAlpha, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(WithTextureAlpha, TEXT(""), MP_Opacity);
	}
	SaveMaterial(Material);
}

void SWGFxMaterials::BuildAll(bool bRebuildExisting)
{
	BuildVariant(TEXT("M_SWGParticle_Additive"), EFxVariant::Additive, bRebuildExisting);
	BuildVariant(TEXT("M_SWGParticle_Translucent"), EFxVariant::Translucent, bRebuildExisting);
	BuildVariant(TEXT("M_SWGParticle_Masked"), EFxVariant::Masked, bRebuildExisting);
	BuildVariant(TEXT("M_SWGParticle_Modulate"), EFxVariant::Modulate, bRebuildExisting);
	BuildVariant(TEXT("M_SWGParticle_Subtractive"), EFxVariant::Subtractive, bRebuildExisting);
	BuildRibbonVariant(TEXT("M_SWGRibbon_Additive"), true, bRebuildExisting);
	BuildRibbonVariant(TEXT("M_SWGRibbon_Translucent"), false, bRebuildExisting);

	// Shader errors only surface once compilation finishes.
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
}
