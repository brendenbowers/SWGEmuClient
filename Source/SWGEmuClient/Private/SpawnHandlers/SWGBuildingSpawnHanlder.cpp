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
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Common/SWGWorldScale.h"

namespace
{
	// How far below the lowest interior floor the terrain pad is placed, in raw
	// units (metres). Enough to keep the ground from z-fighting the floor it sits
	// directly under, small enough not to leave a visible step at the doorway.
	// Note this only buys headroom against a *flat* pad being marginally too
	// high; it does nothing for terrain entering a room through the boundary
	// feather ramp, which is a horizontal problem solved in BuildFlattenLayer by
	// growing the pad outward instead.
	constexpr float TerrainFloorClearance = 0.5f;

	/**
	 * Lowest walkable floor across every *interior* cell, in building-local UE
	 * units — what a terrain pad has to stay under so it doesn't surface inside
	 * a room. Cell 0 is deliberately excluded: it's the exterior shell, whose
	 * geometry can reach well below the rooms (foundations, skirting), and
	 * sinking the pad to that would drop the building into a pit.
	 *
	 * Prefers the cell's .flr walkable floor; falls back to the embedded CMSH
	 * collision mesh when the floor is missing or is one of the older FORM 0003
	 * layouts FSWGFloorReader deliberately refuses to guess at.
	 *
	 * Returns false when no interior cell yields usable geometry, in which case
	 * the caller should leave the pad at the actor origin's height.
	 */
	bool FindLowestInteriorFloorZ(TObjectPtr<USWGTreSubsystem> TreSubsystem, const FSWGPobData& PortalData, float& OutLowestZ)
	{
		if (!TreSubsystem)
		{
			return false;
		}

		bool bFound = false;
		double LowestZ = TNumericLimits<double>::Max();

		for (int32 CellIndex = 1; CellIndex < PortalData.Cells.Num(); ++CellIndex)
		{
			const FSWGPobCell& Cell = PortalData.Cells[CellIndex];

			bool bUsedFloor = false;
			double CellLowest = TNumericLimits<double>::Max();
			const TCHAR* Source = TEXT("none");

			if (!Cell.CollisionFloorPath.IsEmpty())
			{
				FSWGIffReader FloorReader = TreSubsystem->CreateIffReader(Cell.CollisionFloorPath);
				FSWGFloorData FloorData;
				if (FloorReader.IsValid() && FSWGFloorReader::ReadFloor(FloorReader, FloorData) && !FloorData.Vertices.IsEmpty())
				{
					for (const FVector& Vertex : FloorData.Vertices)
					{
						CellLowest = FMath::Min(CellLowest, Vertex.Z);
					}
					bFound = true;
					bUsedFloor = true;
					Source = TEXT("flr");
				}
			}

			if (!bUsedFloor && !Cell.CollisionVertices.IsEmpty())
			{
				for (const FVector& Vertex : Cell.CollisionVertices)
				{
					CellLowest = FMath::Min(CellLowest, Vertex.Z);
				}
				bFound = true;
				Source = TEXT("cmsh");
			}

			UE_LOG(LogTemp, Warning, TEXT("TERRAINPAD   cell[%d] '%s' source=%s lowestZ(UE)=%s floorPath=%s"),
				CellIndex, *Cell.CellName, Source,
				CellLowest == TNumericLimits<double>::Max() ? TEXT("<none>") : *FString::Printf(TEXT("%.2f"), CellLowest),
				*Cell.CollisionFloorPath);

			LowestZ = FMath::Min(LowestZ, CellLowest);
		}

		if (!bFound)
		{
			return false;
		}

		OutLowestZ = (float)LowestZ;
		return true;
	}

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

		if (ASWGCell* CellActor = Cast<ASWGCell>(Actor))
		{
			FBox FloorBounds(ForceInit);
			for (const FVector& Vert : FloorData.Vertices)
			{
				FloorBounds += Vert;
			}

			if (FloorBounds.IsValid)
			{
				constexpr float ApproxRoomHeight = 300.f; // guess; the floor mesh carries no ceiling height
				FloorBounds.Max.Z = FloorBounds.Min.Z + ApproxRoomHeight;

				UBoxComponent* TriggerComp = NewObject<UBoxComponent>(Actor);
				TriggerComp->SetupAttachment(Actor->GetRootComponent());
				TriggerComp->SetBoxExtent(FloorBounds.GetExtent());
				TriggerComp->SetRelativeLocation(FloorBounds.GetCenter());
				TriggerComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				TriggerComp->SetCollisionObjectType(ECC_WorldDynamic);
				TriggerComp->SetCollisionResponseToAllChannels(ECR_Ignore);
				TriggerComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
				TriggerComp->RegisterComponent();
				CellActor->TriggerVolume = TriggerComp;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("CreateCollisionForCell: cell %s has no floor vertices — no trigger volume built"), *CellData.CellName);
			}
		}
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

	if (BuildingActor->PortalData.Cells.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: portal layout file %s has no cells"), *PobPath);
		return false;
	}

	// Now that the portal layout is parsed, stamp this building's terrain edit.
	// It has to come after ReadPob, not before: the pad's height is driven by
	// the building's *lowest interior floor*, which only the POB knows. A
	// building whose rooms sit at different elevations (the cloning facility's
	// back room is below its entrance) would otherwise get a pad at the actor
	// origin's height, which cuts straight through every lower room.
	// No-ops for templates with neither a terrainModificationFileName nor a
	// structureFootprintFileName.
	if (UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr)
	{
		if (USWGTerrainSubsystem* TerrainSubsystem = GameInstance->GetSubsystem<USWGTerrainSubsystem>())
		{
			// The subsystem works in raw/native space (the .trn's own units),
			// not final UE actor coordinates. Yaw needs no handedness fix —
			// SWGWorldScale is a pure scale, with no axis remap.
			const FVector ActorLocation = Actor.GetActorLocation();
			FVector RawPosition = SWGToRawSpace(ActorLocation);
			const float OriginRawZ = (float)RawPosition.Z;

			float LowestFloorZ = 0.0f;
			const bool bFoundFloor = FindLowestInteriorFloorZ(TreSubsystem, BuildingActor->PortalData, LowestFloorZ);
			if (bFoundFloor)
			{
				// Floor geometry is building-local and already in UE units (it
				// reaches the collision mesh unscaled), so it converts to raw
				// before being combined with the raw actor Z.
				RawPosition.Z += SWGToRawSpace(LowestFloorZ) - TerrainFloorClearance;
			}

			UE_LOG(LogTemp, Warning, TEXT("TERRAINPAD %s cells=%d actorUE=(%.1f,%.1f,%.1f) originRawZ=%.3f foundFloor=%d lowestFloorUE=%.2f lowestFloorRaw=%.3f clearance=%.2f -> padRawZ=%.3f yaw=%.1f"),
				*TemplateName, BuildingActor->PortalData.Cells.Num(),
				ActorLocation.X, ActorLocation.Y, ActorLocation.Z,
				OriginRawZ, bFoundFloor ? 1 : 0,
				LowestFloorZ, SWGToRawSpace(LowestFloorZ), TerrainFloorClearance,
				RawPosition.Z, Actor.GetActorRotation().Yaw);

			TerrainSubsystem->ApplyObjectTerrainModification(
				TemplateName,
				RawPosition,
				FMath::DegreesToRadians(Actor.GetActorRotation().Yaw));
		}
	}

	// Cell 0 is always the exterior shell (Sheet 00 §00.2). Keyed by index,
	// not name: most buildings call it "exterior" but plenty just use "r0"
	// (ply_nboo_cloning_facility_s01.pob, thm_tato_cantina.pob, ...). Every
	// other cell is an interior room, built on demand from its own CCLT
	// SceneCreateObjectByCrc/UpdateContainmentMessage pair.
	const FSWGPobCell& ExteriorCell = BuildingActor->PortalData.Cells[0];

	FString CellMeshPath;
	if (!MeshGeneratorSubsystem->ResolveLodMeshPath(ExteriorCell.MeshPath, CellMeshPath) || CellMeshPath.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGBuildingSpawnHandler::HandleActorSpawn: exterior cell '%s' in portal layout file %s has no usable mesh path (raw: %s)"), *ExteriorCell.CellName, *PobPath, *ExteriorCell.MeshPath);
		return true;
	}

	MeshGeneratorSubsystem->RequestMesh(BuildingActor, CellMeshPath);
	CreateCollisionForCell(TreSubsystem, MeshGeneratorSubsystem, BuildingActor, ExteriorCell);


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
			BuildingActor->RegisterCellTrigger(CellActor, CellData.CanSeeParent);
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
