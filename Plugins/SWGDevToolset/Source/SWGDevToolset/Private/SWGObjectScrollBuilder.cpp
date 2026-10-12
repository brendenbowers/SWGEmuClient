// swg.AddScrollToObjectMaterials - adds ScrollUV1/ScrollUV2 to the hand-authored object masters.

#include "SWGObjectScrollBuilder.h"
#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* const MasterNames[] = { TEXT("M_SWGObjectTextured"), TEXT("M_SWGObjectTexturedMasked"), TEXT("M_SWGObjectTranslucent"), TEXT("M_SWGWater") };

	UMaterial* LoadMaster(const TCHAR* Name)
	{
		return LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("/Game/SWGEmu/Materials/%s.%s"), Name, Name));
	}

	void SaveScrollMaster(UMaterial* Material)
	{
		UMaterialEditingLibrary::RecompileMaterial(Material);
		if (const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
		{
			for (const FString& Error : Resource->GetCompileErrors())
			{
				UE_LOG(LogTemp, Error, TEXT("swg.AddScrollToObjectMaterials: %s: %s"), *Material->GetName(), *Error);
			}
		}
		UPackage* Package = Material->GetOutermost();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const bool bSaved = UPackage::SavePackage(Package, Material, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Args);
		UE_LOG(LogTemp, Display, TEXT("swg.AddScrollToObjectMaterials: %s %s"), bSaved ? TEXT("saved") : TEXT("FAILED to save"), *Package->GetName());
	}
}

namespace SWGObjectScroll
{
	void Dump()
	{
		for (const TCHAR* Name : MasterNames)
		{
			UMaterial* Material = LoadMaster(Name);
			if (!Material)
			{
				UE_LOG(LogTemp, Warning, TEXT("ScrollDump: %s missing"), Name);
				continue;
			}
			UE_LOG(LogTemp, Display, TEXT("ScrollDump: %s blend=%d"), Name, (int32)Material->BlendMode);
			for (const TObjectPtr<UMaterialExpression>& Expression : Material->GetExpressions())
			{
				if (!Expression)
				{
					continue;
				}
				FString Line = FString::Printf(TEXT("ScrollDump:   %s %s"), *Expression->GetName(), *Expression->GetClass()->GetName());
				if (const auto* Sample = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
				{
					Line += FString::Printf(TEXT(" param=%s uvs=%s idx=%d"), *Sample->ParameterName.ToString(), Sample->Coordinates.Expression ? *Sample->Coordinates.Expression->GetName() : TEXT("none"), Sample->Coordinates.OutputIndex);
				}
				else if (Expression->HasAParameterName())
				{
					Line += FString::Printf(TEXT(" param=%s"), *Expression->GetParameterName().ToString());
				}
				else
				{
					Line += FString::Printf(TEXT(" desc=%s"), *Expression->GetDescription());
				}
				TArrayView<FExpressionInput*> Inputs = Expression->GetInputsView();
				for (FExpressionInput* Input : Inputs)
				{
					if (Input && Input->Expression)
					{
						Line += FString::Printf(TEXT(" in=%s"), *Input->Expression->GetName());
					}
				}
				UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
			}
		}
	}

	void AddToObjectMaterials()
	{
		for (const TCHAR* Name : { TEXT("M_SWGObjectTextured"), TEXT("M_SWGObjectTexturedMasked") })
		{
			UMaterial* Material = LoadMaster(Name);
			if (!Material)
			{
				UE_LOG(LogTemp, Error, TEXT("swg.AddScrollToObjectMaterials: %s missing"), Name);
				continue;
			}

			UMaterialExpressionTextureSampleParameter2D* Samples[4] = {};
			bool bAlreadyScrolls = false;
			for (const TObjectPtr<UMaterialExpression>& Expression : Material->GetExpressions())
			{
				if (const auto* Vector = Cast<UMaterialExpressionVectorParameter>(Expression))
				{
					bAlreadyScrolls |= Vector->ParameterName == TEXT("ScrollUV1");
				}
				else if (auto* Sample = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
				{
					const int32 SlotIndex = Sample->ParameterName == TEXT("Diffuse") ? 0 : Sample->ParameterName == TEXT("Normal") ? 1
						: Sample->ParameterName == TEXT("Specular") ? 2 : Sample->ParameterName == TEXT("Decal") ? 3 : INDEX_NONE;
					if (SlotIndex != INDEX_NONE)
					{
						Samples[SlotIndex] = Sample;
					}
				}
			}
			if (bAlreadyScrolls)
			{
				UE_LOG(LogTemp, Display, TEXT("swg.AddScrollToObjectMaterials: %s already has ScrollUV1"), Name);
				continue;
			}
			if (!Samples[0])
			{
				UE_LOG(LogTemp, Error, TEXT("swg.AddScrollToObjectMaterials: %s has no Diffuse sample"), Name);
				continue;
			}

			// TexCoord + ScrollUV.rg feeds a sample's UVs.
			auto ScrollUvsFor = [Material](int32 TexCoordIndex, const TCHAR* ParameterName, int32 PositionY)
			{
				auto* TexCoord = Cast<UMaterialExpressionTextureCoordinate>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -1400, PositionY));
				TexCoord->CoordinateIndex = TexCoordIndex;
				auto* Scroll = Cast<UMaterialExpressionVectorParameter>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), -1400, PositionY + 120));
				Scroll->ParameterName = ParameterName;
				Scroll->DefaultValue = FLinearColor(0, 0, 0, 0);
				auto* ScrollRg = Cast<UMaterialExpressionComponentMask>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionComponentMask::StaticClass(), -1200, PositionY + 120));
				ScrollRg->R = true;
				ScrollRg->G = true;
				ScrollRg->B = false;
				ScrollRg->A = false;
				ScrollRg->Input.Connect(0, Scroll);
				auto* Sum = Cast<UMaterialExpressionAdd>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAdd::StaticClass(), -1000, PositionY));
				Sum->A.Connect(0, TexCoord);
				Sum->B.Connect(0, ScrollRg);
				return Sum;
			};

			UMaterialExpressionAdd* LayerOne = ScrollUvsFor(0, TEXT("ScrollUV1"), 2400);
			for (int32 SlotIndex = 0; SlotIndex < 3; ++SlotIndex)
			{
				if (Samples[SlotIndex])
				{
					Samples[SlotIndex]->Coordinates.Connect(0, LayerOne);
				}
			}
			if (Samples[3])
			{
				Samples[3]->Coordinates.Connect(0, ScrollUvsFor(1, TEXT("ScrollUV2"), 2700));
			}
			SaveScrollMaster(Material);
		}
	}
}
