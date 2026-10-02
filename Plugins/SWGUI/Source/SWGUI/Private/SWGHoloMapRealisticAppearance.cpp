#include "SWGHoloMapRealisticAppearance.h"
#include "SWGHoloMapActor.h"
#include "Common/SWGWorldScale.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Subsystems/SWGTerrainSubsystem.h"

void USWGHoloMapRealisticAppearance::OnRegister()
{
	Super::OnRegister();
	ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner());
	if (!Map) { return; }
	OverlayMaterial = Map->MakeHoloMaterial(Map->HoloColor, Map->HoloIntensity * 0.35f);
	TerrainColorTarget = NewObject<UTextureRenderTarget2D>(this);
	TerrainColorTarget->InitCustomFormat(2048, 2048, PF_B8G8R8A8, false);
	TerrainColorTarget->UpdateResourceImmediate(true);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SWGEmu/Materials/M_SWGMapTerrain.M_SWGMapTerrain")))
	{
		TerrainMapMaterial = UMaterialInstanceDynamic::Create(Base, this);
		TerrainMapMaterial->SetTextureParameterValue(TEXT("MapColor"), TerrainColorTarget);
	}
}

void USWGHoloMapRealisticAppearance::OnUnregister()
{
	if (TerrainColorCapture) { TerrainColorCapture->Destroy(); TerrainColorCapture = nullptr; }
	Super::OnUnregister();
}

void USWGHoloMapRealisticAppearance::ApplyTerrain()
{
	ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner());
	if (!Map || !Map->TerrainComponent || !TerrainMapMaterial) { return; }
	Map->TerrainComponent->SetMaterial(0, TerrainMapMaterial);
	Map->TerrainComponent->SetOverlayMaterial(OverlayMaterial);
	if (Map->bHasBake) { CaptureTerrainColor(); }
}

void USWGHoloMapRealisticAppearance::UpdateContourSpacing()
{
	ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner());
	if (Map && OverlayMaterial)
	{
		OverlayMaterial->SetScalarParameterValue(TEXT("ContourSpacing"), Map->ContourMetres * Map->BakedUnitsPerMetre()
			* Map->EffectiveHeightExaggeration() * Map->BakedRadius / Map->ViewRadius);
	}
}

void USWGHoloMapRealisticAppearance::StyleBuilding(UStaticMeshComponent* Building, UStaticMesh* Mesh, const UStaticMeshComponent* Source) const
{
	if (!Building || !Mesh) { return; }
	Building->SetForcedLodModel(FMath::Min(2, Mesh->GetNumLODs()));
	if (OverlayMaterial) { Building->SetOverlayMaterial(OverlayMaterial); }
	if (Source)
	{
		for (int32 Index = 0; Index < Source->GetNumMaterials(); ++Index) { Building->SetMaterial(Index, Source->GetMaterial(Index)); }
	}
}

void USWGHoloMapRealisticAppearance::CaptureTerrainColor()
{
	ASWGHoloMapActor* Map = Cast<ASWGHoloMapActor>(GetOwner());
	UWorld* World = GetWorld();
	USWGTerrainSubsystem* Terrain = Map && Map->GetGameInstance() ? Map->GetGameInstance()->GetSubsystem<USWGTerrainSubsystem>() : nullptr;
	if (!Map || !TerrainColorTarget || !World || !Terrain) { return; }
	if (!TerrainColorCapture)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		TerrainColorCapture = World->SpawnActor<ASceneCapture2D>(Params);
		if (!TerrainColorCapture) { return; }
		USceneCaptureComponent2D* Capture = TerrainColorCapture->GetCaptureComponent2D();
		Capture->ProjectionType = ECameraProjectionMode::Orthographic;
		Capture->CaptureSource = ESceneCaptureSource::SCS_BaseColor;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Capture->TextureTarget = TerrainColorTarget;
	}
	USceneCaptureComponent2D* Capture = TerrainColorCapture->GetCaptureComponent2D();
	TArray<UPrimitiveComponent*> Tiles;
	Terrain->GetTileComponents(Tiles);
	Capture->ShowOnlyComponents.Reset();
	for (UPrimitiveComponent* Tile : Tiles) { Capture->ShowOnlyComponents.Add(Tile); }
	Capture->OrthoWidth = 2.f * Map->BakedRadius * Map->BakeExtentScale * SWGWorldScale;
	float Top = Map->BakedBaseHeight;
	for (float Height : Map->BakedHeights) { Top = FMath::Max(Top, Height); }
	TerrainColorCapture->SetActorLocationAndRotation(
		SWGToUnrealSpace(FVector(Map->BakedCenter.X, Map->BakedCenter.Y, Top + 150.f)), FRotator(-90.f, 0.f, 0.f));
	Capture->CaptureScene();
}
