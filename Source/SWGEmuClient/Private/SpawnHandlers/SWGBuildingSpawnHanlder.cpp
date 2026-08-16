#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"
#include "TRE/SWGPobReader.h"
#include "TRE/SWGFloorReader.h"
#include "Components/PointLightComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"


REGISTER_SWG_ACTOR_SPAWN_HANDLER(FSWGBuildingSpawnHandler, ASWGBuilding)

bool FSWGBuildingSpawnHandler::HandleActorSpawn(AActor& Actor, const FSWGActorSpawnArguments& SpawnInfo)
{

	UWorld* World = Actor.GetWorld();
	Initialize(World);

	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: Failed to initialize subsystems"));
		return false;
	}


	FString TemplateName = SpawnInfo.TemplateName;
	if (TemplateName.IsEmpty())
	{
		TemplateName = TreSubsystem->ResolveTemplatePath(SpawnInfo.TemplateCrc);
	}
	
	if (TemplateName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: Failed to resolve template name"));
		return false;
	}

	FString PobPath;
	if (!MeshGeneratorSubsystem->ResolvePortalLayoutPath(TemplateName, PobPath))
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: template %s has no portalLayoutFilename in its DERV chain"), *TemplateName);
		return false;
	}

	FSWGIffReader Reader = TreSubsystem->CreateIffReader(PobPath);
	if (!Reader.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: failed to open portal layout file %s"), *PobPath);
		return false;
	}

	ASWGBuilding* BuildingActor = Cast<ASWGBuilding>(&Actor);
	if (!FSWGPobReader::ReadPob(Reader, BuildingActor->PortalData))
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: failed to parse portal layout file %s"), *PobPath);
		return false;
	}

	// Cell index 0 is always the exterior/outside cell (Sheet 00 §00.2) —
	// its name varies ("exterior" on the cantina, "r0" on both player
	// houses), so index is the reliable way to find it, not CellName.
	if (BuildingActor->PortalData.Cells.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: portal layout file %s has no cells"), *PobPath);
		return false;
	}


	for (int i = 0; i < BuildingActor->PortalData.Cells.Num(); ++i)
	{
		const FSWGPobCell& CellData = BuildingActor->PortalData.Cells[i];

		FString CellMeshPath;
		if (!MeshGeneratorSubsystem->ResolveLodMeshPath(CellData.MeshPath, CellMeshPath) || CellMeshPath.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: cell %s in portal layout file %s has no usable mesh path (raw: %s)"), *CellData.CellName, *PobPath, *CellData.MeshPath);
			continue;
		}

		if (CellData.CellName == TEXT("exterior"))
		{
			MeshGeneratorSubsystem->RequestMesh(BuildingActor, CellMeshPath);
			CreateCollisionForCell(TreSubsystem, BuildingActor, CellData);
			continue;
		}
		ASWGCell* CellActor = World->SpawnActor<ASWGCell>(ASWGCell::StaticClass(), FTransform::Identity);
		CellActor->CellNumber = CellData.CellIndex;
		CellActor->MeshPath = CellMeshPath;
		CellActor->OwningBuilding = BuildingActor;
		
		BuildingActor->Cells.Add(CellActor);
		CellActor->AttachToActor(BuildingActor, FAttachmentTransformRules::KeepRelativeTransform);

		TWeakObjectPtr<ASWGCell> CellActorWeakPtr = CellActor;
		TObjectPtr<USWGTreSubsystem> TreSubsystemCapture = TreSubsystem; // this (the handler) doesn't survive past HandleActorSpawn returning — can't rely on capturing it
		MeshGeneratorSubsystem->RequestMesh(CellActor, CellMeshPath).Next([CellActorWeakPtr, TreSubsystemCapture](const FSWGMeshGenerationResult& Result)
			{
				if (!CellActorWeakPtr.IsValid())
				{
					UE_LOG(LogTemp, Warning, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: CellActor is no longer valid when mesh generation completed"));
					return;
				}

				ASWGCell* CellActor = CellActorWeakPtr.Get();

				if (!CellActor->OwningBuilding.IsValid())
				{
					UE_LOG(LogTemp, Warning, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: CellActor's OwningBuilding is no longer valid when mesh generation completed"));
					return;
				}

				ASWGBuilding* BuildingActor = CellActor->OwningBuilding.Get();

				// Re-attach: USWGMeshGeneratorSubsystem::BuildGeneratedMeshComponent
				// just replaced CellActor's root with the newly-built render mesh
				// component
				CellActor->AttachToActor(BuildingActor, FAttachmentTransformRules::KeepWorldTransform);

				FSWGPobCell CellData = BuildingActor->PortalData.Cells[CellActor->CellNumber];

				for(int i = 0; i < CellData.Lights.Num(); ++i)
				{
					FSWGPobLight& LightData = CellData.Lights[i];

					ULightComponent* LightComp = nullptr;

					if (LightData.Type == ESWGPobLightType::Point)
					{
						UPointLightComponent* PointLight = NewObject<UPointLightComponent>(CellActor);
					
						FVector BoxCenter, BoxExtent;
						CellActor->GetActorBounds(false, BoxCenter, BoxExtent);
						const float CircumRadius = BoxExtent.Size();
						const float LightToCenter = (LightData.Transform.GetLocation() - BoxCenter).Size();
						const float AttenuationRadius = LightToCenter + CircumRadius;

						PointLight->AttenuationRadius = AttenuationRadius;
						LightComp = PointLight;
					}
					else
					{
						continue; // Skip non-point lights for now
					}
					//else // Ambient or Parallel — no spatially-bounded UE equivalent, fake it as a soft fill
					//{
					//	UPointLightComponent* FillLight = NewObject<UPointLightComponent>(CellActor);
					//	FillLight->bUseInverseSquaredFalloff = false;   // flat falloff instead of physical 1/d^2
					//	FillLight->CastShadows = false;
					//	FillLight->AttenuationRadius = 800.f;            // big enough to blanket a room; tune per-cell if you have bounds
					//	LightComp = FillLight;
					//}

					LightComp->SetRelativeTransform(LightData.Transform);
					LightComp->SetLightColor(LightData.DiffuseColor);
					LightComp->RegisterComponent();
					LightComp->AttachToComponent(CellActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
				}
				CreateCollisionForCell(TreSubsystemCapture, CellActor, CellData);
			});

	}


	return true;
}


void FSWGBuildingSpawnHandler::CreateCollisionForCell(TObjectPtr<USWGTreSubsystem> TreSubsystem, AActor* Actor, const FSWGPobCell& CellData)
{
	FSWGIffReader FloorReader = TreSubsystem->CreateIffReader(CellData.CollisionFloorPath);
	FSWGFloorData FloorData;
	const bool bFloorParsed = FloorReader.IsValid() && FSWGFloorReader::ReadFloor(FloorReader, FloorData);
	if (bFloorParsed && !FloorData.Triangles.IsEmpty())
	{
		UDynamicMeshComponent* FloorCollisionComp = NewObject<UDynamicMeshComponent>(Actor);
		FloorCollisionComp->SetupAttachment(Actor->GetRootComponent());
		FloorCollisionComp->EditMesh([&FloorData](FDynamicMesh3& EditMesh)
			{
				TArray<int32> VertexIds;
				VertexIds.Reserve(FloorData.Vertices.Num());
				for (const FVector& Vertex : FloorData.Vertices)
				{
					VertexIds.Add(EditMesh.AppendVertex(Vertex));
				}
				for (const FSWGFloorTriangle& Tri : FloorData.Triangles)
				{
					EditMesh.AppendTriangle(
						VertexIds[Tri.CornerIndex1],
						VertexIds[Tri.CornerIndex2],
						VertexIds[Tri.CornerIndex3]);
				}
			});

		FloorCollisionComp->SetVisibility(false);
		FloorCollisionComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		FloorCollisionComp->SetCollisionObjectType(ECC_WorldStatic);
		FloorCollisionComp->SetCollisionResponseToAllChannels(ECR_Block);
		FloorCollisionComp->RegisterComponent();
		FloorCollisionComp->EnableComplexAsSimpleCollision();
		UE_LOG(LogTemp, Warning, TEXT("FSWGBuildingSpawnHandler: cell %s floor collision component registered, bounds=%s"), *CellData.CellName, *FloorCollisionComp->Bounds.GetBox().ToString());
	}
	else if (FloorReader.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: failed to parse floor file %s for cell %s"), *CellData.CollisionFloorPath, *CellData.CellName);
	}
}

void FSWGBuildingSpawnHandler::Initialize(UWorld* World)
{
	if (bIsInitialized)
	{
		return;
	}

	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::Initialize: World is null"));
		return;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::Initialize: GameInstance is null"));
		return;
	}

	TreSubsystem = GameInstance->GetSubsystem<USWGTreSubsystem>();
	MeshGeneratorSubsystem = GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>();

	bIsInitialized = TreSubsystem && MeshGeneratorSubsystem;
}
