#include "SWGHoloMapHologramAppearance.h"
#include "SWGHoloMapActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

void USWGHoloMapHologramAppearance::OnRegister()
{
	Super::OnRegister();
	if (ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner()))
	{
		HoloMaterial = Map->MakeHoloMaterial(Map->HoloColor, Map->HoloIntensity);
	}
}

void USWGHoloMapHologramAppearance::ApplyTerrain()
{
	if (ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner()))
	{
		Map->TerrainComponent->SetMaterial(0, HoloMaterial);
		Map->TerrainComponent->SetOverlayMaterial(nullptr);
	}
}

void USWGHoloMapHologramAppearance::UpdateContourSpacing()
{
	ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner());
	if (Map && HoloMaterial)
	{
		HoloMaterial->SetScalarParameterValue(TEXT("ContourSpacing"), Map->ContourMetres * Map->BakedUnitsPerMetre()
			* Map->EffectiveHeightExaggeration() * Map->BakedRadius / Map->ViewRadius);
	}
}

void USWGHoloMapHologramAppearance::StyleBuilding(UStaticMeshComponent* Building, UStaticMesh* Mesh, const UStaticMeshComponent* Source) const
{
	if (!Building || !Mesh) { return; }
	Building->SetForcedLodModel(Mesh->GetNumLODs());
	for (int32 Index = 0; Index < Mesh->GetStaticMaterials().Num(); ++Index) { Building->SetMaterial(Index, HoloMaterial); }
}
