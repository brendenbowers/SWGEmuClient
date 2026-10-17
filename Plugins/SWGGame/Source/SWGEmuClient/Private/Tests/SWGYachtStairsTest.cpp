#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SWGMovementComponent.h"
#include "Objects/Player/SWGPlayer.h"
#include "Objects/World/SWGShip.h"
#include "Objects/World/SWGCell.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGActorSpawnHandlerRegistry.h"
#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "TRE/SWGFloorReader.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGDoorStyleRow.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
class FSWGYachtStairCommand : public IAutomationLatentCommand
{
public:
	explicit FSWGYachtStairCommand(TFunction<bool()> InUpdate) : UpdateFunction(MoveTemp(InUpdate)) {}
	bool Update() override { return UpdateFunction(); }
private:
	TFunction<bool()> UpdateFunction;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGYachtStairsTest, "SWG.Space.YachtStairs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSWGYachtStairsTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->AddToRoot(); Game->InitializeStandalone();
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World) { Game->Shutdown(); Game->RemoveFromRoot(); return false; }
	World->AddToRoot(); Game->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(Game);
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	auto Cleanup = [Game, World] { Game->Shutdown(); World->DestroyWorld(false); World->RemoveFromRoot(); Game->RemoveFromRoot(); };
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	USWGMeshGeneratorSubsystem* Meshes = Game->GetSubsystem<USWGMeshGeneratorSubsystem>();
	if (!Tre || !Meshes) { Cleanup(); return false; }
	Tre->LoadConfig();
	if (!TestTrue(TEXT("retail archives available"), Tre->LoadArchives())) { Cleanup(); return false; }
	FSWGDataTableData DoorStyles;
	if (!TestTrue(TEXT("door styles load"), FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/appearance/door_style.iff")), DoorStyles))
		|| !TestNotNull(TEXT("door table initializes"), SWGDoorStyle::BuildDataTable(DoorStyles))) { Cleanup(); return false; }
	ASWGShip* Ship = World->SpawnActor<ASWGShip>();
	if (!Ship || !TestTrue(TEXT("normal yacht spawn succeeds"), FSWGActorSpawnHandlerRegistry::Get().TryHandle(*Ship,
		{0, ASWGShip::StaticClass(), TEXT("object/ship/player/shared_player_sorosuub_space_yacht.iff")}))) { Cleanup(); return false; }
	const FSWGPobCell* StairData = Ship->PortalData.Cells.FindByPredicate([](const FSWGPobCell& Cell) { return Cell.CellName == TEXT("stairwell"); });
	FSWGFloorData Floor;
	if (!StairData || !TestTrue(TEXT("actual stairwell floor parses"), FSWGFloorReader::ReadFloor(Tre->CreateIffReader(StairData->CollisionFloorPath), Floor))) { Cleanup(); return false; }
	TArray<ASWGCell*> Rooms;
	ASWGCell* Stairs = nullptr;
	for (const FSWGPobCell& Cell : Ship->PortalData.Cells)
	{
		if (Cell.CellIndex == 0) continue;
		ASWGCell* Room = World->SpawnActor<ASWGCell>();
		if (!Room) { Cleanup(); return false; }
		Rooms.Add(Room);
		if (Cell.CellIndex == StairData->CellIndex) Stairs = Room;
		FSWGCellSpawnHandler::FinishCell(Room,Ship,Cell.CellIndex,Tre,Meshes,true);
	}
	if (!TestNotNull(TEXT("stairwell cell spawns"), Stairs)) { Cleanup(); return false; }
	const double Started = FPlatformTime::Seconds();
	AddCommand(new FSWGYachtStairCommand([this, World, Ship, Stairs, Rooms, Meshes, Floor=MoveTemp(Floor), Started, Cleanup]() mutable
	{
		Meshes->Tick(1.f/60.f); World->Tick(LEVELTICK_All, 1.f/60.f);
		if (Rooms.ContainsByPredicate([](const ASWGCell* Room) { return !Room->bCollisionReady; }))
		{
			if (FPlatformTime::Seconds()-Started < 120) return false;
			AddError(TEXT("actual yacht room collision did not finish")); Cleanup(); return true;
		}
		ASWGPlayer* Walker = World->SpawnActor<ASWGPlayer>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		if (!Walker || !Controller) { AddError(TEXT("walking fixture failed to spawn")); Cleanup(); return true; }
		Controller->SetAsLocalPlayerController();
		Controller->Possess(Walker);
		Walker->GetCapsuleComponent()->SetCapsuleSize(34,88);
		USWGMovementComponent* Movement = Walker->GetSWGMovementComponent();
		FCreatureObjectBaseline Baseline;
		Baseline.RunSpeed = 1.55f; Baseline.WalkSpeed = 1.55f;
		Baseline.SpeedMultiplierBase = Baseline.SpeedMultiplierMod = 1;
		Baseline.AccelerationMultiplierBase = 10; Baseline.AccelerationMultiplierMod = 1;
		Baseline.SlopeModAngle = FMath::DegreesToRadians(50.f); Baseline.TurnScale = 1;
		Movement->ApplyBase4(Baseline);
		TestTrue(TEXT("walking fixture is locally controlled"), Walker->IsLocallyControlled());
		TestTrue(TEXT("walking component ticks"), Movement->IsComponentTickEnabled());
		TSet<int32> Visited;
		int32 Flights = 0;
		for (int32 Seed = 0; Seed < Floor.Triangles.Num(); ++Seed)
		{
			const FVector Normal = Floor.Triangles[Seed].Normal.GetSafeNormal();
			if (Visited.Contains(Seed) || FMath::Abs(Normal.Z) < .65 || FMath::Abs(Normal.Z) > .8) continue;
			TArray<int32> Pending{Seed}; TSet<int32> Vertices;
			while (!Pending.IsEmpty())
			{
				const int32 Index = Pending.Pop();
				if (Visited.Contains(Index)) continue;
				Visited.Add(Index);
				const FSWGFloorTriangle& Triangle = Floor.Triangles[Index];
				Vertices.Add(Triangle.CornerIndex1); Vertices.Add(Triangle.CornerIndex2); Vertices.Add(Triangle.CornerIndex3);
				for (int32 Neighbor : {Triangle.NeighborIndex1, Triangle.NeighborIndex2, Triangle.NeighborIndex3})
					if (Floor.Triangles.IsValidIndex(Neighbor) && !Visited.Contains(Neighbor)
						&& Floor.Triangles[Neighbor].Normal.GetSafeNormal().Equals(Normal,.001)) Pending.Add(Neighbor);
			}
			double MinZ = TNumericLimits<double>::Max(), MaxZ = -TNumericLimits<double>::Max();
			for (int32 Index : Vertices) { MinZ = FMath::Min(MinZ, Floor.Vertices[Index].Z); MaxZ = FMath::Max(MaxZ, Floor.Vertices[Index].Z); }
			if (MaxZ-MinZ < 100) continue;
			FVector Bottom = FVector::ZeroVector, Top = FVector::ZeroVector; int32 BottomCount = 0, TopCount = 0;
			for (int32 Index : Vertices)
			{
				if (Floor.Vertices[Index].Z < MinZ+1) { Bottom += Floor.Vertices[Index]; ++BottomCount; }
				if (Floor.Vertices[Index].Z > MaxZ-1) { Top += Floor.Vertices[Index]; ++TopCount; }
			}
			Bottom /= BottomCount; Top /= TopCount;
			const FVector Along = Top-Bottom;
			const float Inset = 45.f / Along.Size2D();
			const FVector StartFeet = FMath::Lerp(Bottom,Top,Inset), EndFeet = FMath::Lerp(Bottom,Top,1-Inset);
			++Flights;
			for (bool bAscending : {true,false})
			{
				const FVector From = bAscending ? StartFeet : EndFeet, To = bAscending ? EndFeet : StartFeet;
				// A capsule's rounded foot needs clearance above a sloping plane before settling.
				Walker->SetActorLocation(From+FVector(0,0,110), false, nullptr, ETeleportType::TeleportPhysics);
				Movement->StopMovementImmediately(); Movement->SetMovementMode(MOVE_Falling);
				// World tick functions run once per engine frame; step the real movement component for this bounded probe.
				for (int32 Frame = 0; Frame < 60; ++Frame) Movement->TickComponent(1.f/60.f,LEVELTICK_All,&Movement->PrimaryComponentTick);
				const FVector Direction = (To-From).GetSafeNormal2D();
				const int32 FrameLimit = FMath::CeilToInt(FVector::Dist2D(From,To)/Movement->MaxWalkSpeed*60)+120;
				for (int32 Frame = 0; Frame < FrameLimit && FVector::DotProduct(To-Walker->GetActorLocation(),Direction) > 5; ++Frame)
				{
					Walker->AddMovementInput(Direction,1,true);
					Movement->TickComponent(1.f/60.f,LEVELTICK_All,&Movement->PrimaryComponentTick);
				}
				const FVector Feet = Walker->GetActorLocation()-FVector(0,0,88);
				AddInfo(FString::Printf(TEXT("stair flight %d %s: from=%s to=%s feet=%s floor=%s normal=%s mode=%d"),
					Flights, bAscending ? TEXT("up") : TEXT("down"), *From.ToString(), *To.ToString(), *Feet.ToString(),
					*GetNameSafe(Movement->CurrentFloor.HitResult.GetComponent()), *Movement->CurrentFloor.HitResult.ImpactNormal.ToString(), int32(Movement->MovementMode)));
				TestTrue(TEXT("capsule traverses the actual yacht stair flight"), FVector::Dist2D(Feet,To) < 20 && FMath::Abs(Feet.Z-To.Z) < 25);
			}
		}
		TestTrue(TEXT("actual stairwell contains tested ramp flights"), Flights > 0);
		// Approach from the observation floor through the lower doorway, then climb and cross the upper landing.
		const TArray<FVector> Route{FVector(1390,500,-366.955), FVector(1390,320,-346.955), FVector(1390,-220,3.045), FVector(1640,-220,3.045)};
		for (float Speed : {1.55f,5.376f})
		{
			Baseline.RunSpeed = Speed; Movement->ApplyBase4(Baseline);
			for (bool bAscending : {true,false})
			{
				const FVector Start = bAscending ? Route[0] : Route.Last();
				Walker->SetActorLocation(Start+FVector(0,0,110),false,nullptr,ETeleportType::TeleportPhysics);
				Movement->StopMovementImmediately(); Movement->SetMovementMode(MOVE_Falling);
				for (int32 Frame = 0; Frame < 60; ++Frame) Movement->TickComponent(1.f/60.f,LEVELTICK_All,&Movement->PrimaryComponentTick);
				for (int32 Leg = 1; Leg < Route.Num(); ++Leg)
				{
					const FVector Target = Route[bAscending ? Leg : Route.Num()-1-Leg];
					const int32 FrameLimit = FMath::CeilToInt(FVector::Dist2D(Walker->GetActorLocation(),Target)/Movement->MaxWalkSpeed*60)+120;
					for (int32 Frame = 0; Frame < FrameLimit && FVector::Dist2D(Walker->GetActorLocation(),Target) > 5; ++Frame)
					{
						Walker->AddMovementInput((Target-Walker->GetActorLocation()).GetSafeNormal2D(),1,true);
						Movement->TickComponent(1.f/60.f,LEVELTICK_All,&Movement->PrimaryComponentTick);
					}
					const FVector Feet = Walker->GetActorLocation()-FVector(0,0,88);
					AddInfo(FString::Printf(TEXT("stair landing %s speed=%g leg=%d target=%s feet=%s normal=%s mode=%d"),
						bAscending ? TEXT("up") : TEXT("down"),Speed,Leg,*Target.ToString(),*Feet.ToString(),
						*Movement->CurrentFloor.HitResult.ImpactNormal.ToString(),int32(Movement->MovementMode)));
					TestTrue(TEXT("capsule traverses yacht landing and raised threshold"), FVector::Dist2D(Feet,Target) < 20 && FMath::Abs(Feet.Z-Target.Z) < 25);
				}
			}
		}
		Cleanup(); return true;
	}));
	return true;
}
#endif
