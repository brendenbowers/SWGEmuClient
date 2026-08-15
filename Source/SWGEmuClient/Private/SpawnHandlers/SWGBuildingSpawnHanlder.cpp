


#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"
#include "TRE/SWGPobReader.h"
#include "Components/PointLightComponent.h"


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
		if (CellData.CellName == TEXT("exterior"))
		{
			MeshGeneratorSubsystem->RequestMesh(BuildingActor, CellData.MeshPath);
			continue;
		}

		FString CellMeshPath = CellData.MeshPath;
		if (CellMeshPath.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: cell %s in portal layout file %s has no mesh path"), *CellData.CellName, *PobPath);
			continue;
		}
		ASWGCell* CellActor = World->SpawnActor<ASWGCell>(ASWGCell::StaticClass(), FTransform::Identity);
		CellActor->OwningBuilding = BuildingActor;
		
		BuildingActor->Cells.Add(CellActor);

		TWeakObjectPtr<ASWGCell> CellActorWeakPtr = CellActor;	
		MeshGeneratorSubsystem->RequestMesh(CellActor, CellMeshPath).Next([CellActorWeakPtr](const FSWGMeshGenerationResult& Result)
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

				CellActor->AttachToActor(BuildingActor, FAttachmentTransformRules::KeepRelativeTransform);

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
						const float CircumRadius = BoxExtent.Size();                                   // center -> farthest corner
						const float LightToCenter = (LightData.Transform.GetLocation() - BoxCenter).Size();
						const float AttenuationRadius = LightToCenter + CircumRadius;                  // upper bound from the light's actual position

						PointLight->AttenuationRadius = AttenuationRadius;
						LightComp = PointLight;
					}
					else // Ambient or Parallel — no spatially-bounded UE equivalent, fake it as a soft fill
					{
						UPointLightComponent* FillLight = NewObject<UPointLightComponent>(CellActor);
						FillLight->bUseInverseSquaredFalloff = false;   // flat falloff instead of physical 1/d^2
						FillLight->CastShadows = false;
						FillLight->AttenuationRadius = 800.f;            // big enough to blanket a room; tune per-cell if you have bounds
					}

					LightComp->SetRelativeTransform(LightData.Transform);
					LightComp->SetLightColor(LightData.DiffuseColor);
					LightComp->RegisterComponent();
					LightComp->AttachToComponent(CellActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
				}

			});

	}


	return true;
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
