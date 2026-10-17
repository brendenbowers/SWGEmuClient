#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Components/SWGEquipmentComponent.h"
#include "Components/SWGSpacePlanetComponent.h"
#include "Objects/Creature/SWGCreature.h"
#include "Objects/World/SWGShip.h"
#include "Objects/World/SWGCell.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "TRE/SWGSpaceTerrainReader.h"
#include "TimerManager.h"
#include "Objects/World/SWGStaticProp.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGZoneSceneReadinessTest, "SWG.Flow.ZoneSceneReadiness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGZoneSceneReadinessTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->AddToRoot();
	Game->InitializeStandalone();
	UWorld* World = Game->GetWorld();
	USWGTerrainSubsystem* Terrain = Game->GetSubsystem<USWGTerrainSubsystem>();
	USWGMeshGeneratorSubsystem* Meshes = Game->GetSubsystem<USWGMeshGeneratorSubsystem>();
	if (!World || !Terrain || !Meshes) { Game->Shutdown(); Game->RemoveFromRoot(); return false; }
	World->AddToRoot();
	TestFalse(TEXT("unparsed terrain cannot reveal"), Terrain->IsSpawnSceneReady());
	Terrain->bTerrainDataCached = true;
	Terrain->InitialTiles.Add(FIntPoint::ZeroValue);
	Terrain->InitialSnapshotTiles.Add(FIntPoint::ZeroValue);
	auto& Tile = Terrain->Tiles.Add(FIntPoint::ZeroValue);
	Tile.LiveEditVersion = Tile.WantedEditVersion = 0;
	TestFalse(TEXT("first collision bake must complete"), Terrain->IsSpawnSceneReady());
	Terrain->bInitialTilesReported = true;
	TestFalse(TEXT("snapshot resolve must finish even with terrain ready"), Terrain->IsSpawnSceneReady());
	Tile.bSnapshotSpawned = true;
	Tile.bSnapshotResolveInFlight = true;
	TestFalse(TEXT("queued snapshot flag does not bypass resolver"), Terrain->IsSpawnSceneReady());
	Tile.bSnapshotResolveInFlight = false;
	Tile.PendingSnapshotObjects.AddDefaulted();
	TestFalse(TEXT("snapshot actors must actually spawn"), Terrain->IsSpawnSceneReady());
	Tile.PendingSnapshotObjects.Reset();
	TestTrue(TEXT("empty resolved scene can reveal"), Terrain->IsSpawnSceneReady());
	Tile.WantedEditVersion = 1;
	TestFalse(TEXT("late building pads and holes must rebake before reveal"), Terrain->IsSpawnSceneReady());
	Tile.LiveEditVersion = 1;
	Tile.bBakeInFlight = true;
	TestFalse(TEXT("a current edit version still waits for its running bake"), Terrain->IsSpawnSceneReady());
	Tile.bBakeInFlight = false;

	ASWGStaticProp* Prop = World->SpawnActor<ASWGStaticProp>();
	Tile.SnapshotActors.Add(Prop);
	Meshes->RequestMeshForTemplatePath(Prop, TEXT("object/loading-test.iff"));
	TestFalse(TEXT("spawned actor waits for its queued appearance"), Terrain->IsSpawnSceneReady());
	{
		FSWGPendingMeshRequest Parsing = Meshes->PendingRequests.Pop();
		TestFalse(TEXT("empty queue does not imply parse/finalization complete"), Terrain->IsSpawnSceneReady());
		FSWGPendingMeshRequest Finalizing = MoveTemp(Parsing);
		TestFalse(TEXT("request ownership survives continuation handoff"), Terrain->IsSpawnSceneReady());
	}
	TestTrue(TEXT("completed or failed request releases loading gate"), Terrain->IsSpawnSceneReady());

	ASWGStaticProp* Remote = World->SpawnActor<ASWGStaticProp>();
	Remote->SetActorLocation(FVector(10000000, 10000000, 0));
	Meshes->RequestMeshForTemplatePath(Remote, TEXT("object/remote-test.iff"));
	TestTrue(TEXT("unrelated distant requests cannot prolong spawn loading"), Terrain->IsSpawnSceneReady());
	Meshes->RequestMeshForTemplatePath(Prop, TEXT("object/loading-test.iff"));
	TestFalse(TEXT("another nearby request keeps gate closed"), Terrain->IsSpawnSceneReady());
	Meshes->PendingRequests.Reset();
	TestTrue(TEXT("cancelled requests release loading gate"), Terrain->IsSpawnSceneReady());
	ASWGCreature* Creature = World->SpawnActor<ASWGCreature>();
	Creature->EquipmentComponent->AttachMeshToHardpoint(1, LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")), FSWGMeshData(), {}, 239);
	Creature->EquipmentComponent->AttachWearableSkeletalMesh(2, NewObject<USkeletalMesh>(), {}, 239);
	TestFalse(TEXT("equipment body-wait timers retain loading after generation callbacks return"), Terrain->IsSpawnSceneReady());
	{
		TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter + 1);
		World->GetTimerManager().Tick(0.f);
		++GFrameCounter;
		World->GetTimerManager().Tick(.3f);
	}
	TestTrue(TEXT("final attachment retry failure releases equipment loading tokens"), Terrain->IsSpawnSceneReady());

	USWGObjectGraphSubsystem* Graph = Game->GetSubsystem<USWGObjectGraphSubsystem>();
	ASWGShip* Ship = World->SpawnActor<ASWGShip>();
	ASWGCell* Room = World->SpawnActor<ASWGCell>();
	Graph->LocalPlayerObjectId = 7001;
	Graph->ActorRegistry.Add(7001, Creature);
	Graph->ActorRegistry.Add(7002, Ship);
	Graph->ActorRegistry.Add(7003, Room);
	Graph->ContainerByObjectId.Add(7001, 7003);
	Graph->ContainerByObjectId.Add(7003, 7002);
	Room->OwningBuilding = Ship;
	Ship->Cells.Add(Room);
	Terrain->SpaceData = MakeShared<FSWGSpaceTerrainData>();
	TestTrue(TEXT("space scene has no pending local appearances"), Terrain->IsSpawnSceneReady());
	Terrain->SpacePlanet = NewObject<USWGSpacePlanetComponent>(Ship);
	Terrain->SpacePlanet->Region.bPending = true;
	TestFalse(TEXT("space loading waits for the planet texture bake and game-thread application"), Terrain->IsSpawnSceneReady());
	Terrain->SpacePlanet->Region.bPending = false;
	TestTrue(TEXT("completed or failed planet preparation releases the loading gate"), Terrain->IsSpawnSceneReady());
	{
		auto PlayerWork = Meshes->TrackLoading(Creature);
		TestFalse(TEXT("space loading waits for local player body or equipment"), Terrain->IsSpawnSceneReady());
	}
	{
		auto RoomWork = Meshes->TrackLoading(Room);
		TestFalse(TEXT("space loading waits for containing room appearance"), Terrain->IsSpawnSceneReady());
	}
	{
		auto ShipWork = Meshes->TrackLoading(Ship);
		TestFalse(TEXT("space loading waits for containing ship appearance"), Terrain->IsSpawnSceneReady());
	}
	Room->InteriorActors.Add(Prop);
	{
		auto FurnitureWork = Meshes->TrackLoading(Prop);
		TestFalse(TEXT("space loading waits for unattached interior layout furniture"), Terrain->IsSpawnSceneReady());
	}
	{
		auto RemoteWork = Meshes->TrackLoading(Remote);
		TestTrue(TEXT("space loading ignores unrelated scene actors"), Terrain->IsSpawnSceneReady());
	}
	Terrain->SpaceData.Reset();
	Meshes->RequestMeshForTemplatePath(Prop, TEXT("object/loading-test.iff"));
	Prop->Destroy();
	TestTrue(TEXT("destroyed actors cannot hold loading screen"), Terrain->IsSpawnSceneReady());
	Meshes->PendingRequests.Reset();
	Terrain->FinishSpawnSceneLoading();
	TestFalse(TEXT("reveal releases initial synchronous-collision mode"), Terrain->bSpawnSceneLoading);
	Terrain->ResetZone();
	TestTrue(TEXT("zone reset restores initial loading mode"), Terrain->bSpawnSceneLoading);
	TestTrue(TEXT("zone reset forgets previous snapshot ring"), Terrain->InitialSnapshotTiles.IsEmpty());
	TestFalse(TEXT("previous scene readiness cannot leak across travel"), Terrain->IsSpawnSceneReady());
	Game->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	Game->RemoveFromRoot();
	return true;
}
#endif
