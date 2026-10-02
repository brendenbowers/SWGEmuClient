// swg.CreateSkyMaterials — builds the sky-sprite (alpha) and cloud-layer
// parent materials under /Game/SWGEmu/Materials so the runtime can
// instance them. The other SWG materials were authored by hand in the
// editor; these two are generated here so their graphs are reproducible.

#include "SWGSkyMaterialBuilder.h"
#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionArctangent2.h"
#include "Materials/MaterialExpressionArcsine.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "MaterialEditingLibrary.h"
#include "AssetToolsModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Engine/Texture2D.h"

namespace
{
	UMaterial* CreateSkyMaterialAsset(const FString& Name)
	{
		const FString PackagePath = TEXT("/Game/SWGEmu/Materials/") + Name;
		if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *Name)))
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.CreateSkyMaterials: %s already exists — delete it first to regenerate"), *Name);
			return nullptr;
		}
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		return Cast<UMaterial>(AssetTools.CreateAsset(Name, TEXT("/Game/SWGEmu/Materials"), UMaterial::StaticClass(), Factory));
	}

	template <typename T>
	T* AddExpression(UMaterial* Material, int32 X, int32 Y)
	{
		return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(Material, T::StaticClass(), X, Y));
	}

	UTexture2D* DefaultTexture()
	{
		return LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	}

	void SaveMaterial(UMaterial* Material)
	{
		UMaterialEditingLibrary::RecompileMaterial(Material);
		UPackage* Package = Material->GetOutermost();
		const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Material, *FileName, Args);
		UE_LOG(LogTemp, Log, TEXT("swg.CreateSkyMaterials: saved %s"), *Package->GetName());
	}

	/** Unlit translucent: Emissive = Diffuse.RGB * Intensity, Opacity = Diffuse.A. Moons and celestial props. */
	void BuildSkySpriteAlpha()
	{
		UMaterial* Material = CreateSkyMaterialAsset(TEXT("M_SWGSkySpriteAlpha"));
		if (!Material) return;

		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;

		auto* Diffuse = AddExpression<UMaterialExpressionTextureSampleParameter2D>(Material, -500, 0);
		Diffuse->ParameterName = TEXT("Diffuse");
		Diffuse->Texture = DefaultTexture();
		auto* Intensity = AddExpression<UMaterialExpressionScalarParameter>(Material, -500, 250);
		Intensity->ParameterName = TEXT("Intensity");
		Intensity->DefaultValue = 1.0f;
		auto* Multiply = AddExpression<UMaterialExpressionMultiply>(Material, -250, 0);

		UMaterialEditingLibrary::ConnectMaterialExpressions(Diffuse, TEXT("RGB"), Multiply, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Intensity, TEXT(""), Multiply, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Multiply, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Diffuse, TEXT("A"), MP_Opacity);
		SaveMaterial(Material);
	}

	/**
	 * Unlit translucent cloud sheet: world-space UVs (WorldPosition.xy /
	 * TileSize) scrolling by Speed per second so a camera-following plane
	 * still reads as clouds drifting. Emissive = Diffuse.RGB * Tint,
	 * Opacity = Diffuse.A * Opacity.
	 */
	void BuildCloudLayer()
	{
		UMaterial* Material = CreateSkyMaterialAsset(TEXT("M_SWGCloudLayer"));
		if (!Material) return;

		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;

		auto* WorldPosition = AddExpression<UMaterialExpressionWorldPosition>(Material, -1300, 0);
		auto* Mask = AddExpression<UMaterialExpressionComponentMask>(Material, -1100, 0);
		Mask->R = true; Mask->G = true; Mask->B = false; Mask->A = false;
		auto* TileSize = AddExpression<UMaterialExpressionScalarParameter>(Material, -1100, 150);
		TileSize->ParameterName = TEXT("TileSize");
		TileSize->DefaultValue = 9000.0f;
		auto* Divide = AddExpression<UMaterialExpressionDivide>(Material, -900, 0);
		auto* Time = AddExpression<UMaterialExpressionTime>(Material, -1100, 300);
		auto* Speed = AddExpression<UMaterialExpressionScalarParameter>(Material, -1100, 400);
		Speed->ParameterName = TEXT("Speed");
		Speed->DefaultValue = 0.01f;
		auto* Scroll = AddExpression<UMaterialExpressionMultiply>(Material, -900, 300);
		auto* Offset = AddExpression<UMaterialExpressionAdd>(Material, -700, 0);
		auto* Diffuse = AddExpression<UMaterialExpressionTextureSampleParameter2D>(Material, -500, 0);
		Diffuse->ParameterName = TEXT("Diffuse");
		Diffuse->Texture = DefaultTexture();
		auto* Tint = AddExpression<UMaterialExpressionVectorParameter>(Material, -500, 250);
		Tint->ParameterName = TEXT("Tint");
		Tint->DefaultValue = FLinearColor::White;
		auto* Color = AddExpression<UMaterialExpressionMultiply>(Material, -250, 0);
		auto* Opacity = AddExpression<UMaterialExpressionScalarParameter>(Material, -500, 450);
		Opacity->ParameterName = TEXT("Opacity");
		Opacity->DefaultValue = 1.0f;
		auto* Alpha = AddExpression<UMaterialExpressionMultiply>(Material, -250, 300);

		// Horizon fade: the sheet thins out with horizontal distance from the
		// camera so its far edge and the grazing-angle compression never show.
		auto* CameraPosition = AddExpression<UMaterialExpressionCameraPositionWS>(Material, -1300, 600);
		auto* ToCamera = AddExpression<UMaterialExpressionSubtract>(Material, -1100, 600);
		auto* FlatOffset = AddExpression<UMaterialExpressionComponentMask>(Material, -900, 600);
		FlatOffset->R = true; FlatOffset->G = true; FlatOffset->B = false; FlatOffset->A = false;
		auto* Distance = AddExpression<UMaterialExpressionDistance>(Material, -700, 600);
		auto* Origin = AddExpression<UMaterialExpressionConstant2Vector>(Material, -900, 750);
		auto* FadeDistance = AddExpression<UMaterialExpressionScalarParameter>(Material, -700, 750);
		FadeDistance->ParameterName = TEXT("FadeDistance");
		FadeDistance->DefaultValue = 2500000.0f;
		auto* FadeRatio = AddExpression<UMaterialExpressionDivide>(Material, -500, 650);
		auto* FadeInverse = AddExpression<UMaterialExpressionOneMinus>(Material, -350, 650);
		auto* Fade = AddExpression<UMaterialExpressionSaturate>(Material, -200, 650);
		auto* FadedAlpha = AddExpression<UMaterialExpressionMultiply>(Material, -50, 400);

		UMaterialEditingLibrary::ConnectMaterialExpressions(WorldPosition, TEXT(""), Mask, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Mask, TEXT(""), Divide, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(TileSize, TEXT(""), Divide, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Time, TEXT(""), Scroll, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Speed, TEXT(""), Scroll, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Divide, TEXT(""), Offset, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Scroll, TEXT(""), Offset, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Offset, TEXT(""), Diffuse, TEXT("UVs"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Diffuse, TEXT("RGB"), Color, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Tint, TEXT("RGB"), Color, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Diffuse, TEXT("A"), Alpha, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Opacity, TEXT(""), Alpha, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(WorldPosition, TEXT(""), ToCamera, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(CameraPosition, TEXT(""), ToCamera, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(ToCamera, TEXT(""), FlatOffset, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialExpressions(FlatOffset, TEXT(""), Distance, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Origin, TEXT(""), Distance, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Distance, TEXT(""), FadeRatio, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(FadeDistance, TEXT(""), FadeRatio, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(FadeRatio, TEXT(""), FadeInverse, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialExpressions(FadeInverse, TEXT(""), Fade, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Alpha, TEXT(""), FadedAlpha, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Fade, TEXT(""), FadedAlpha, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Color, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(FadedAlpha, TEXT(""), MP_Opacity);
		SaveMaterial(Material);
	}

	/**
	 * Unlit additive star dome: the view direction mapped to equirectangular
	 * UVs (atan2 for longitude, asin for latitude) into a generated star
	 * texture, scaled by Intensity (the night fade). Emissive only.
	 */
	void BuildStarField()
	{
		UMaterial* Material = CreateSkyMaterialAsset(TEXT("M_SWGStarField"));
		if (!Material) return;

		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Additive;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		Material->bIsSky = true;

		auto* CameraPosition = AddExpression<UMaterialExpressionCameraPositionWS>(Material, -1500, 0);
		auto* WorldPosition = AddExpression<UMaterialExpressionWorldPosition>(Material, -1500, 150);
		auto* ToPixel = AddExpression<UMaterialExpressionSubtract>(Material, -1300, 80);
		auto* Direction = AddExpression<UMaterialExpressionNormalize>(Material, -1150, 80);
		auto* DirX = AddExpression<UMaterialExpressionComponentMask>(Material, -1000, 0);
		DirX->R = true; DirX->G = false; DirX->B = false; DirX->A = false;
		auto* DirY = AddExpression<UMaterialExpressionComponentMask>(Material, -1000, 100);
		DirY->R = false; DirY->G = true; DirY->B = false; DirY->A = false;
		auto* DirZ = AddExpression<UMaterialExpressionComponentMask>(Material, -1000, 200);
		DirZ->R = false; DirZ->G = false; DirZ->B = true; DirZ->A = false;
		auto* Longitude = AddExpression<UMaterialExpressionArctangent2>(Material, -850, 50);
		auto* Latitude = AddExpression<UMaterialExpressionArcsine>(Material, -850, 200);
		auto* LongitudeScale = AddExpression<UMaterialExpressionConstant>(Material, -850, -80);
		LongitudeScale->R = 1.0f / (2.0f * UE_PI);
		auto* LatitudeScale = AddExpression<UMaterialExpressionConstant>(Material, -850, 300);
		LatitudeScale->R = -1.0f / UE_PI;
		auto* U = AddExpression<UMaterialExpressionMultiply>(Material, -700, 50);
		auto* V = AddExpression<UMaterialExpressionMultiply>(Material, -700, 200);
		auto* Half = AddExpression<UMaterialExpressionConstant>(Material, -700, 350);
		Half->R = 0.5f;
		auto* UCentered = AddExpression<UMaterialExpressionAdd>(Material, -550, 50);
		auto* VCentered = AddExpression<UMaterialExpressionAdd>(Material, -550, 200);
		auto* UV = AddExpression<UMaterialExpressionAppendVector>(Material, -400, 100);
		auto* Stars = AddExpression<UMaterialExpressionTextureSampleParameter2D>(Material, -250, 100);
		Stars->ParameterName = TEXT("Stars");
		Stars->Texture = DefaultTexture();
		auto* Intensity = AddExpression<UMaterialExpressionScalarParameter>(Material, -250, 350);
		Intensity->ParameterName = TEXT("Intensity");
		Intensity->DefaultValue = 1.0f;
		auto* Output = AddExpression<UMaterialExpressionMultiply>(Material, -50, 150);

		UMaterialEditingLibrary::ConnectMaterialExpressions(WorldPosition, TEXT(""), ToPixel, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(CameraPosition, TEXT(""), ToPixel, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(ToPixel, TEXT(""), Direction, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Direction, TEXT(""), DirX, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Direction, TEXT(""), DirY, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Direction, TEXT(""), DirZ, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialExpressions(DirY, TEXT(""), Longitude, TEXT("Y"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(DirX, TEXT(""), Longitude, TEXT("X"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(DirZ, TEXT(""), Latitude, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Longitude, TEXT(""), U, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(LongitudeScale, TEXT(""), U, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Latitude, TEXT(""), V, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(LatitudeScale, TEXT(""), V, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(U, TEXT(""), UCentered, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Half, TEXT(""), UCentered, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(V, TEXT(""), VCentered, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Half, TEXT(""), VCentered, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(UCentered, TEXT(""), UV, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(VCentered, TEXT(""), UV, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(UV, TEXT(""), Stars, TEXT("UVs"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Stars, TEXT("RGB"), Output, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Intensity, TEXT(""), Output, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Output, TEXT(""), MP_EmissiveColor);
		SaveMaterial(Material);
	}
}

void SWGSkyMaterials::BuildAll()
{
	BuildSkySpriteAlpha();
	BuildCloudLayer();
	BuildStarField();
}
