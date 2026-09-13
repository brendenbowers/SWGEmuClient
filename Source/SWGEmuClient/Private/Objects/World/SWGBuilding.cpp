#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"
#include "Objects/Player/SWGPlayer.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/SWGConditionComponent.h"
#include "Components/SWGDefenderComponent.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "Common/SWGWorldScale.h"
#include "Engine/GameInstance.h"

ASWGBuilding::ASWGBuilding()
{
	PrimaryActorTick.bCanEverTick = false;

	TangibleComponent = CreateDefaultSubobject<USWGTangibleComponent>(TEXT("TangibleComponent"));
	ConditionComponent = CreateDefaultSubobject<USWGConditionComponent>(TEXT("ConditionComponent"));
	DefenderComponent = CreateDefaultSubobject<USWGDefenderComponent>(TEXT("DefenderComponent"));
}

void ASWGBuilding::RegisterCellTrigger(ASWGCell* Cell, bool bCanSeeParent)
{
	if (!Cell || !Cell->TriggerVolume)
	{
		UE_LOG(LogTemp, Warning, TEXT("ASWGBuilding::RegisterCellTrigger: %s — no trigger volume for cell %s"),
			*GetName(), Cell ? *Cell->GetName() : TEXT("null"));
		return;
	}

	CellSeeParentByTrigger.Add(Cell->TriggerVolume, bCanSeeParent);

	Cell->TriggerVolume->OnComponentBeginOverlap.AddDynamic(this, &ASWGBuilding::OnCellTriggerBeginOverlap);
	Cell->TriggerVolume->OnComponentEndOverlap.AddDynamic(this, &ASWGBuilding::OnCellTriggerEndOverlap);
}

void ASWGBuilding::OnCellTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Local player only — this is a client-side view effect, not gameplay state.
	if (!Cast<ASWGPlayer>(OtherActor))
	{
		return;
	}

	++InteriorOverlapCount;

	const bool* bCanSeeParent = CellSeeParentByTrigger.Find(OverlappedComponent);
	if (bCanSeeParent && !*bCanSeeParent)
	{
		if (++NonSeeThroughOverlapCount == 1)
		{
			SetExteriorShellHidden(true);
		}
	}
}

void ASWGBuilding::OnCellTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!Cast<ASWGPlayer>(OtherActor))
	{
		return;
	}

	InteriorOverlapCount = FMath::Max(0, InteriorOverlapCount - 1);

	const bool* bCanSeeParent = CellSeeParentByTrigger.Find(OverlappedComponent);
	if (bCanSeeParent && !*bCanSeeParent)
	{
		if (--NonSeeThroughOverlapCount <= 0)
		{
			NonSeeThroughOverlapCount = 0;
			SetExteriorShellHidden(false);
		}
	}
}

FBox2D ASWGBuilding::GetFootprint() const
{
	// Own components only — cells are attached actors, and a starport's
	// courtyard is inside the shell's box anyway. Before the mesh lands this
	// is just the root, which degrades to the origin.
	const FBox Bounds = GetComponentsBoundingBox(/*bNonColliding*/ true);
	return FBox2D(FVector2D(Bounds.Min.X, Bounds.Min.Y), FVector2D(Bounds.Max.X, Bounds.Max.Y));
}

bool ASWGBuilding::OwnsCell(int64 CellObjectId) const
{
	if (CellObjectId == 0)
	{
		return false;
	}
	for (const FSWGWorldSnapshotSpawnInfo& Info : DeferredSnapshotCells)
	{
		if (Info.ObjectId == CellObjectId)
		{
			return true;
		}
	}
	for (const FDeferredNetworkCell& Deferred : DeferredNetworkCells)
	{
		if (const ASWGCell* Cell = Deferred.Cell.Get(); Cell && Cell->GetObjectId() == CellObjectId)
		{
			return true;
		}
	}
	for (const ASWGCell* Cell : Cells)
	{
		if (Cell && Cell->GetObjectId() == CellObjectId)
		{
			return true;
		}
	}
	return false;
}

bool ASWGBuilding::IsClosedRoom(int32 CellIndex) const
{
	return PortalData.Cells.IsValidIndex(CellIndex) && !PortalData.Cells[CellIndex].CanSeeParent;
}

const TArray<ASWGBuilding::FEntrance>& ASWGBuilding::GetEntrances()
{
	if (bEntrancesBuilt)
	{
		return Entrances;
	}
	bEntrancesBuilt = true;

	for (int32 CellIndex = 1; CellIndex < PortalData.Cells.Num(); ++CellIndex)
	{
		const FSWGPobCell& Cell = PortalData.Cells[CellIndex];

		// Which way is "out" for this room: away from its own floor.
		FVector RoomCenter = FVector::ZeroVector;
		for (const FVector& V : Cell.CollisionVertices)
		{
			RoomCenter += V;
		}
		if (!Cell.CollisionVertices.IsEmpty())
		{
			RoomCenter /= Cell.CollisionVertices.Num();
		}

		for (const FSWGPobPortalRef& Portal : Cell.Portals)
		{
			if (Portal.ConnectingCellIndex != 0 || Portal.OpeningVertices.Num() < 3)
			{
				continue;
			}

			// Newell's method: robust for the non-planar/concave openings
			// some doorways have, where a single triangle's cross product isn't.
			FVector Center = FVector::ZeroVector;
			FVector Normal = FVector::ZeroVector;
			const int32 Count = Portal.OpeningVertices.Num();
			for (int32 i = 0; i < Count; ++i)
			{
				const FVector& A = Portal.OpeningVertices[i];
				const FVector& B = Portal.OpeningVertices[(i + 1) % Count];
				Center += A;
				Normal += FVector((A.Y - B.Y) * (A.Z + B.Z), (A.Z - B.Z) * (A.X + B.X), (A.X - B.X) * (A.Y + B.Y));
			}
			Center /= Count;
			if (!Normal.Normalize())
			{
				continue;
			}
			if (FVector::DotProduct(Normal, Center - RoomCenter) < 0.0f)
			{
				Normal = -Normal;
			}

			Entrances.Add({ CellIndex, Center, Normal });
		}
	}

	return Entrances;
}

bool ASWGBuilding::IsRoomLoaded(int32 CellIndex) const
{
	for (const ASWGCell* Cell : Cells)
	{
		if (Cell && Cell->CellNumber == CellIndex)
		{
			return true;
		}
	}
	return false;
}

bool ASWGBuilding::HasDeferredRoom(int32 CellIndex) const
{
	for (const FDeferredNetworkCell& Deferred : DeferredNetworkCells)
	{
		if (Deferred.CellIndex == CellIndex)
		{
			return true;
		}
	}
	for (const FSWGWorldSnapshotSpawnInfo& Info : DeferredSnapshotCells)
	{
		if (Info.CellNumber == CellIndex)
		{
			return true;
		}
	}
	return false;
}

void ASWGBuilding::LoadRoom(int32 CellIndex)
{
	if (IsRoomLoaded(CellIndex))
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	USWGTreSubsystem* Tre = GameInstance->GetSubsystem<USWGTreSubsystem>();
	USWGMeshGeneratorSubsystem* MeshGen = GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>();
	USWGTerrainSubsystem* Terrain = GameInstance->GetSubsystem<USWGTerrainSubsystem>();
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance->GetSubsystem<USWGObjectGraphSubsystem>();

	// A network cell is finished in place and leaves the deferred list for good.
	for (auto It = DeferredNetworkCells.CreateIterator(); It; ++It)
	{
		if (It->CellIndex != CellIndex)
		{
			continue;
		}
		if (ASWGCell* Cell = It->Cell.Get())
		{
			FSWGCellSpawnHandler::FinishCell(Cell, this, CellIndex, Tre, MeshGen, /*bForceInterior*/ true);
		}
		It.RemoveCurrent();
		return;
	}

	// A .ws room is spawned from its node, which stays deferred so an unload can be reversed.
	if (!Terrain)
	{
		return;
	}
	for (const FSWGWorldSnapshotSpawnInfo& CellInfo : DeferredSnapshotCells)
	{
		if (CellInfo.CellNumber != CellIndex)
		{
			continue;
		}
		const FTransform CellRelative(CellInfo.Rotation, SWGToUnrealSpace(CellInfo.Position));
		if (ASWGCell* Cell = Cast<ASWGCell>(Terrain->SpawnWorldSnapshotNode(CellInfo, CellRelative * SnapshotTransform, this, ObjectGraph, /*bForceInterior*/ true)))
		{
			StreamedCells.Add(Cell);
		}
		return;
	}
}

void ASWGBuilding::UnloadRoom(int32 CellIndex)
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;

	auto Unregister = [ObjectGraph](AActor* Actor)
		{
			const ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(Actor);
			if (ObjectGraph && NetObject)
			{
				ObjectGraph->UnregisterStaticObject(NetObject->GetObjectId());
			}
		};

	// Only .ws rooms are ours to destroy; a network cell is the server's.
	for (auto It = StreamedCells.CreateIterator(); It; ++It)
	{
		ASWGCell* Cell = It->Get();
		if (!Cell)
		{
			It.RemoveCurrent();
			continue;
		}
		if (Cell->CellNumber != CellIndex)
		{
			continue;
		}

		for (const TWeakObjectPtr<AActor>& PropWeak : Cell->InteriorActors)
		{
			if (AActor* Prop = PropWeak.Get())
			{
				Unregister(Prop);
				Prop->Destroy();
			}
		}

		// Network items attached to the room are the server's; Destroy just
		// detaches them, and RegisterStaticObject re-attaches on the next load.
		Cells.Remove(Cell);
		CellSeeParentByTrigger.Remove(Cell->TriggerVolume);
		Unregister(Cell);
		Cell->Destroy();
		It.RemoveCurrent();
		return;
	}
}

void ASWGBuilding::SetExteriorShellHidden(bool bShouldHide)
{
	// Visibility only
	SetActorHiddenInGame(bShouldHide);
}
