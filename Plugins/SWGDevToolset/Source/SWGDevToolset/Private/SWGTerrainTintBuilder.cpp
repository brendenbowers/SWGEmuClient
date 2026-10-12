// swg.AddTintToTerrainMaterial - feeds the retail ground tint (terrain mesh UV1/UV2) into M_SWGTerrainBlend's base colour.

#include "SWGTerrainTintBuilder.h"
#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* const TintNodeDescription = TEXT("SWGGroundTint");

	UMaterial* LoadTerrainMaterial()
	{
		return LoadObject<UMaterial>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGTerrainBlend.M_SWGTerrainBlend"));
	}
}

namespace SWGTerrainTintMaterial
{
	void Dump()
	{
		UMaterial* Material = LoadTerrainMaterial();
		if (!Material)
		{
			UE_LOG(LogTemp, Warning, TEXT("TerrainTintDump: M_SWGTerrainBlend missing"));
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("TerrainTintDump: blend=%d"), static_cast<int32>(Material->BlendMode));
		for (const TObjectPtr<UMaterialExpression>& Expression : Material->GetExpressions())
		{
			if (!Expression) continue;
			FString Line = FString::Printf(TEXT("TerrainTintDump:   %s %s"), *Expression->GetName(), *Expression->GetClass()->GetName());
			if (Expression->HasAParameterName())
			{
				Line += FString::Printf(TEXT(" param=%s"), *Expression->GetParameterName().ToString());
			}
			else
			{
				Line += FString::Printf(TEXT(" desc=%s"), *Expression->GetDescription());
			}
			for (FExpressionInput* Input : Expression->GetInputsView())
			{
				if (Input && Input->Expression)
				{
					Line += FString::Printf(TEXT(" in=%s[%d]"), *Input->Expression->GetName(), Input->OutputIndex);
				}
			}
			UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		}
		const FColorMaterialInput& BaseColor = Material->GetEditorOnlyData()->BaseColor;
		UE_LOG(LogTemp, Display, TEXT("TerrainTintDump: base colour <- %s[%d]"), BaseColor.Expression ? *BaseColor.Expression->GetName() : TEXT("none"), BaseColor.OutputIndex);
	}

	void AddToTerrainMaterial()
	{
		UMaterial* Material = LoadTerrainMaterial();
		if (!Material)
		{
			UE_LOG(LogTemp, Error, TEXT("swg.AddTintToTerrainMaterial: M_SWGTerrainBlend missing"));
			return;
		}

		for (const TObjectPtr<UMaterialExpression>& Expression : Material->GetExpressions())
		{
			if (Expression && Expression->GetDescription() == TintNodeDescription)
			{
				UE_LOG(LogTemp, Display, TEXT("swg.AddTintToTerrainMaterial: already tinted"));
				return;
			}
		}

		const FColorMaterialInput& BaseColor = Material->GetEditorOnlyData()->BaseColor;
		UMaterialExpression* OldBaseColor = BaseColor.Expression;
		const int32 OldOutputIndex = BaseColor.OutputIndex;
		if (!OldBaseColor)
		{
			UE_LOG(LogTemp, Error, TEXT("swg.AddTintToTerrainMaterial: base colour is not connected"));
			return;
		}

		// UV1 = (R,G), UV2.R = B, written by BuildTerrainTileGeometry.
		auto* TexCoordRg = Cast<UMaterialExpressionTextureCoordinate>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -1600, 2600));
		TexCoordRg->CoordinateIndex = 1;
		auto* MaskRg = Cast<UMaterialExpressionComponentMask>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionComponentMask::StaticClass(), -1400, 2600));
		MaskRg->R = true; MaskRg->G = true; MaskRg->B = false; MaskRg->A = false;
		MaskRg->Input.Connect(0, TexCoordRg);

		auto* TexCoordB = Cast<UMaterialExpressionTextureCoordinate>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -1600, 2800));
		TexCoordB->CoordinateIndex = 2;
		auto* MaskB = Cast<UMaterialExpressionComponentMask>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionComponentMask::StaticClass(), -1400, 2800));
		MaskB->R = true; MaskB->G = false; MaskB->B = false; MaskB->A = false;
		MaskB->Input.Connect(0, TexCoordB);

		auto* Append = Cast<UMaterialExpressionAppendVector>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAppendVector::StaticClass(), -1200, 2700));
		Append->A.Connect(0, MaskRg);
		Append->B.Connect(0, MaskB);

		auto* Multiply = Cast<UMaterialExpressionMultiply>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), -1000, 2700));
		Multiply->Desc = TintNodeDescription;
		Multiply->A.Connect(OldOutputIndex, OldBaseColor);
		Multiply->B.Connect(0, Append);
		UMaterialEditingLibrary::ConnectMaterialProperty(Multiply, FString(), MP_BaseColor);

		UMaterialEditingLibrary::RecompileMaterial(Material);
		if (const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
		{
			for (const FString& Error : Resource->GetCompileErrors())
			{
				UE_LOG(LogTemp, Error, TEXT("swg.AddTintToTerrainMaterial: %s"), *Error);
			}
		}
		UPackage* Package = Material->GetOutermost();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const bool bSaved = UPackage::SavePackage(Package, Material, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Args);
		UE_LOG(LogTemp, Display, TEXT("swg.AddTintToTerrainMaterial: %s %s"), bSaved ? TEXT("saved") : TEXT("FAILED to save"), *Package->GetName());
	}
}
