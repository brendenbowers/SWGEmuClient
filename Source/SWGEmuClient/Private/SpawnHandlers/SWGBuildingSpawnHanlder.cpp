#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"
#include "Objects/World/SWGDoor.h"
#include "TRE/SWGPobReader.h"
#include "TRE/SWGFloorReader.h"
#include "TRE/SWGDoorStyleRow.h"
#include "Engine/DataTable.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"

namespace
{
	// Owned by neither spawn concept — bakes floor collision from FSWGPobCell
	// data onto Actor (the building itself for the exterior shell, or a cell
	// actor once FSWGCellSpawnHandler::FinishCell builds it).
	void CreateCollisionForCell(TObjectPtr<USWGTreSubsystem> TreSubsystem, TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem, AActor* Actor, const FSWGPobCell& CellData)
	{
		FSWGIffReader FloorReader = TreSubsystem->CreateIffReader(CellData.CollisionFloorPath);
		FSWGFloorData FloorData;
		const bool bFloorParsed = FloorReader.IsValid() && FSWGFloorReader::ReadFloor(FloorReader, FloorData);
		if (!bFloorParsed || FloorData.Triangles.IsEmpty())
		{
			if (FloorReader.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("CreateCollisionForCell: failed to parse floor file %s for cell %s"), *CellData.CollisionFloorPath, *CellData.CellName);
			}
			return;
		}

		const uint32 CacheHash = GetTypeHash(CellData.CollisionFloorPath);

		TArray<int32> FlatIndices;
		FlatIndices.Reserve(FloorData.Triangles.Num() * 3);
		for (const FSWGFloorTriangle& Tri : FloorData.Triangles)
		{
			FlatIndices.Add(Tri.CornerIndex1);
			FlatIndices.Add(Tri.CornerIndex2);
			FlatIndices.Add(Tri.CornerIndex3);
		}

		UStaticMesh* CollisionMesh = MeshGeneratorSubsystem->GetOrBuildGeneratedCollisionMesh(CacheHash, CellData.CollisionFloorPath, FloorData.Vertices, FlatIndices);
		if (!CollisionMesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("CreateCollisionForCell: failed to get/build cached collision mesh for cell %s floor %s"), *CellData.CellName, *CellData.CollisionFloorPath);
			return;
		}

		UStaticMeshComponent* FloorCollisionComp = NewObject<UStaticMeshComponent>(Actor);
		FloorCollisionComp->SetupAttachment(Actor->GetRootComponent());
		FloorCollisionComp->SetStaticMesh(CollisionMesh);
		FloorCollisionComp->SetVisibility(false);
		FloorCollisionComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		FloorCollisionComp->SetCollisionObjectType(ECC_WorldStatic);
		FloorCollisionComp->SetCollisionResponseToAllChannels(ECR_Block);
		FloorCollisionComp->RegisterComponent();
	}
}

REGISTER_SWG_ACTOR_SPAWN_HANDLER(FSWGBuildingSpawnHandler, ASWGBuilding)
REGISTER_SWG_ACTOR_SPAWN_HANDLER(FSWGCellSpawnHandler, ASWGCell)

TWeakObjectPtr<UDataTable> FSWGCellSpawnHandler::GetDoorStyleTable()
{
	static TWeakObjectPtr<UDataTable> CachedTable;
	if (!CachedTable.IsValid())
	{
		CachedTable = LoadObject<UDataTable>(nullptr, SWGDoorStyle::DataTablePath);
	}
	return CachedTable;
}

bool FSWGCellSpawnHandler::HandleActorSpawn(AActor& Actor, const FSWGActorSpawnArguments& SpawnInfo)
{
	ASWGCell* CellActor = Cast<ASWGCell>(&Actor);
	UGameInstance* GameInstance = Actor.GetWorld() ? Actor.GetWorld()->GetGameInstance() : nullptr;
	if (!CellActor || !GameInstance)
	{
		return true;
	}

	USWGObjectGraphSubsystem* ObjectGraph = GameInstance->GetSubsystem<USWGObjectGraphSubsystem>();
	if (!ObjectGraph)
	{
		return true;
	}

	CheckAndFinishCell(*ObjectGraph, CellActor->GetObjectId(),
		GameInstance->GetSubsystem<USWGTreSubsystem>(), GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>());

	return true;
}

void FSWGCellSpawnHandler::CheckAndFinishCell(USWGObjectGraphSubsystem& ObjectGraph, int64 ObjectId, TObjectPtr<USWGTreSubsystem> TreSubsystem, TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem)
{
	ASWGCell* CellActor = Cast<ASWGCell>(ObjectGraph.FindActor(ObjectId));
	if (!CellActor || CellActor->OwningBuilding.IsValid())
	{
		return;
	}

	const int64* ContainerId = ObjectGraph.FindContainerId(ObjectId);
	const int32* CellNumber = ObjectGraph.FindCellNumber(ObjectId);
	if (!ContainerId || !CellNumber)
	{
		return; // still waiting on containment and/or the TLCS baseline
	}

	if (AActor* ContainerActor = ObjectGraph.FindActor(*ContainerId))
	{
		if (ASWGBuilding* BuildingActor = Cast<ASWGBuilding>(ContainerActor))
		{
			FinishCell(CellActor, BuildingActor, *CellNumber, TreSubsystem, MeshGeneratorSubsystem);
		}
		return;
	}

	// Owning building hasn't spawned yet — finish once it's ready rather than
	// polling.
	TWeakObjectPtr<ASWGCell> WeakCell = CellActor;
	TWeakObjectPtr<USWGObjectGraphSubsystem> WeakObjectGraph = &ObjectGraph;
	const int64 ContainerIdCopy = *ContainerId;
	TSharedPtr<FDelegateHandle> Handle = MakeShared<FDelegateHandle>();
	*Handle = ObjectGraph.OnObjectReady.AddLambda([WeakObjectGraph, WeakCell, ContainerIdCopy, Handle, TreSubsystem, MeshGeneratorSubsystem](int64 ReadyObjectId)
		{
			if (ReadyObjectId != ContainerIdCopy || !WeakObjectGraph.IsValid())
			{
				return;
			}

			WeakObjectGraph->OnObjectReady.Remove(*Handle);

			if (ASWGCell* Cell = WeakCell.Get())
			{
				CheckAndFinishCell(*WeakObjectGraph.Get(), Cell->GetObjectId(), TreSubsystem, MeshGeneratorSubsystem);
			}
		});
}

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
	// its name varies
	if (BuildingActor->PortalData.Cells.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: portal layout file %s has no cells"), *PobPath);
		return false;
	}


	for (int i = 0; i < BuildingActor->PortalData.Cells.Num(); ++i)
	{
		const FSWGPobCell& CellData = BuildingActor->PortalData.Cells[i];

		if (CellData.CellName != TEXT("exterior"))
		{
			// Interior cells are built on demand, from their own CCLT
			// SceneCreateObjectByCrc/UpdateContainmentMessage pair
			continue;
		}

		FString CellMeshPath;
		if (!MeshGeneratorSubsystem->ResolveLodMeshPath(CellData.MeshPath, CellMeshPath) || CellMeshPath.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: cell %s in portal layout file %s has no usable mesh path (raw: %s)"), *CellData.CellName, *PobPath, *CellData.MeshPath);
			continue;
		}

		MeshGeneratorSubsystem->RequestMesh(BuildingActor, CellMeshPath);
		CreateCollisionForCell(TreSubsystem, MeshGeneratorSubsystem, BuildingActor, CellData);
	}


	return true;
}

void FSWGCellSpawnHandler::FinishCell(ASWGCell* CellActor, ASWGBuilding* BuildingActor, int32 CellIndex, TObjectPtr<USWGTreSubsystem> TreSubsystem, TObjectPtr<USWGMeshGeneratorSubsystem> MeshGeneratorSubsystem)
{
	if (!CellActor || !BuildingActor || CellActor->OwningBuilding.IsValid())
	{
		// Null args, or already finished (e.g. a duplicate containment message).
		return;
	}

	if (!BuildingActor->PortalData.Cells.IsValidIndex(CellIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::FinishCell: cell index %d out of range for building %s (%d cells)"), CellIndex, *BuildingActor->GetName(), BuildingActor->PortalData.Cells.Num());
		return;
	}

	const FSWGPobCell& CellData = BuildingActor->PortalData.Cells[CellIndex];

	FString CellMeshPath;
	if (!MeshGeneratorSubsystem->ResolveLodMeshPath(CellData.MeshPath, CellMeshPath) || CellMeshPath.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::FinishCell: cell %s has no usable mesh path (raw: %s)"), *CellData.CellName, *CellData.MeshPath);
		return;
	}

	CellActor->CellNumber = CellData.CellIndex;
	CellActor->MeshPath = CellMeshPath;
	CellActor->OwningBuilding = BuildingActor;

	BuildingActor->Cells.Add(CellActor);
	CellActor->AttachToActor(BuildingActor, FAttachmentTransformRules::KeepRelativeTransform);

	TWeakObjectPtr<ASWGCell> CellActorWeakPtr = CellActor;
	MeshGeneratorSubsystem->RequestMesh(CellActor, CellMeshPath).Next([CellActorWeakPtr, TreSubsystem, MeshGeneratorSubsystem](const FSWGMeshGenerationResult& Result)
		{
			if (!CellActorWeakPtr.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::FinishCell: CellActor is no longer valid when mesh generation completed"));
				return;
			}

			ASWGCell* CellActor = CellActorWeakPtr.Get();

			if (!CellActor->OwningBuilding.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::FinishCell: CellActor's OwningBuilding is no longer valid when mesh generation completed"));
				return;
			}

			ASWGBuilding* BuildingActor = CellActor->OwningBuilding.Get();

			FSWGPobCell CellData = BuildingActor->PortalData.Cells[CellActor->CellNumber];
			if (Result.MeshOrComponent.IsType<FEmptyVariantState>())
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::FinishCell: Failed to create or laod mesh for cell %s"), *CellData.CellName);
				BuildingActor->Cells.Remove(CellActor);
				CellActor->Destroy();
				return;
			}

			// Re-attach: USWGMeshGeneratorSubsystem::BuildGeneratedMeshComponent
			// just replaced CellActor's root with the newly-built render mesh
			// component
			CellActor->AttachToActor(BuildingActor, FAttachmentTransformRules::KeepWorldTransform);

			for (int i = 0; i < CellData.Lights.Num(); ++i)
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

				LightComp->SetRelativeTransform(LightData.Transform);
				LightComp->SetLightColor(LightData.DiffuseColor);
				LightComp->RegisterComponent();
				LightComp->AttachToComponent(CellActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			}
			CreateCollisionForCell(TreSubsystem, MeshGeneratorSubsystem, CellActor, CellData);
		});

	UWorld* World = CellActor->GetWorld();
	for (int j = 0; j < CellData.Portals.Num(); j++)
	{
		const FSWGPobPortalRef& PortalRef = CellData.Portals[j];
		if (PortalRef.DoorStyle.IsEmpty())
		{
			continue; // open archway, no door object
		}

		FString DoorMeshPath;
		if (!MeshGeneratorSubsystem->ResolveLodMeshPath(TEXT("appearance/lod/") + PortalRef.DoorStyle + TEXT(".lod"), DoorMeshPath) || DoorMeshPath.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGCellSpawnHandler::FinishCell: portal %d has no usable mesh path for door style %s"), PortalRef.PortalNumber, *PortalRef.DoorStyle);
			continue;
		}

		if (!BuildingActor->Doors.ContainsByPredicate([&PortalRef](const TObjectPtr<ASWGDoor> DoorObj) { return DoorObj->PortalNumber == PortalRef.PortalNumber; }))
		{
			ASWGDoor* DoorActor = World->SpawnActor<ASWGDoor>(ASWGDoor::StaticClass(), FTransform::Identity);
			BuildingActor->Doors.Add(DoorActor);
			DoorActor->PortalNumber = PortalRef.PortalNumber;
			DoorActor->AttachToActor(BuildingActor, FAttachmentTransformRules::KeepRelativeTransform);
			DoorActor->SetActorRelativeTransform(PortalRef.DoorHardpoint);

			TWeakObjectPtr<ASWGDoor> DoorActorWeakPtr = DoorActor;
			MeshGeneratorSubsystem->RequestMesh(DoorActor, DoorMeshPath).Next([DoorActorWeakPtr, PortalRef, OwningBuilding = CellActor->OwningBuilding](const FSWGMeshGenerationResult& Result)
				{
					if (!DoorActorWeakPtr.IsValid() || !OwningBuilding.IsValid())
					{
						return;
					}

					if (Result.MeshOrComponent.IsType<FEmptyVariantState>())
					{
						OwningBuilding->Doors.Remove(DoorActorWeakPtr.Get());
						DoorActorWeakPtr->Destroy();
						return;
					}

					DoorActorWeakPtr->AttachToActor(OwningBuilding.Get(), FAttachmentTransformRules::KeepWorldTransform);

					const FSWGDoorStyleRow* StyleRow = nullptr;
					if (TWeakObjectPtr<UDataTable> DoorStyleTable = FSWGCellSpawnHandler::GetDoorStyleTable(); DoorStyleTable.IsValid())
					{
						StyleRow = DoorStyleTable->FindRow<FSWGDoorStyleRow>(FName(*PortalRef.DoorStyle), TEXT("FSWGCellSpawnHandler::FinishCell"), false);
					}
					DoorActorWeakPtr->InitializeDoorStyle(StyleRow);
				});
		}
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
