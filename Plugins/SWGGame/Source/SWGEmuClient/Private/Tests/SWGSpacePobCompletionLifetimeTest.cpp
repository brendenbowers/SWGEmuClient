#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "UObject/GarbageCollection.h"
#include "Objects/World/SWGShip.h"
#include "Objects/World/SWGCell.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "TRE/SWGPobReader.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpacePobCompletionLifetimeTest, "SWG.Space.PobCompletionLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGSpacePobCompletionLifetimeTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->AddToRoot(); Game->InitializeStandalone();
	UWorld* World = Game->GetWorldContext()->World();
	if (!World) { Game->Shutdown(); Game->RemoveFromRoot(); return false; }
	World->InitializeActorsForPlay(FURL());
	USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
	USWGMeshGeneratorSubsystem* Meshes = Game->GetSubsystem<USWGMeshGeneratorSubsystem>();
	Tre->LoadConfig();
	FSWGPobData Yacht;
	if (!Tre->LoadArchives() || !FSWGPobReader::ReadPob(Tre->CreateIffReader(TEXT("appearance/sorosuub_space_yacht.pob")), Yacht)
		|| !Yacht.Cells.IsValidIndex(6))
	{
		AddError(TEXT("real yacht room fixture unavailable"));
		Game->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
		World->RemoveFromRoot(); Game->RemoveFromRoot(); return false;
	}
	// Exercise FinishCell's real future/GT continuation, independently of mesh-import timing.
	Yacht.Cells[6].Portals.Reset();
	ASWGShip* Ship = World->SpawnActor<ASWGShip>(); Ship->PortalData = Yacht;
	auto QueueRoom = [&]()
	{
		ASWGCell* Cell = World->SpawnActor<ASWGCell>();
		FSWGCellSpawnHandler::FinishCell(Cell, Ship, 6, Tre, Meshes, true);
		return Cell;
	};
	auto TakePromise = [&](ASWGCell* Cell)
	{
		const int32 Index = Meshes->PendingRequests.IndexOfByPredicate([Cell](const FSWGPendingMeshRequest& Request) { return Request.Actor.Get() == Cell; });
		TestTrue(TEXT("real room mesh request queued"), Index != INDEX_NONE);
		TUniquePtr<TPromise<FSWGMeshGenerationResult>> Promise;
		if (Index != INDEX_NONE) { Promise = MoveTemp(Meshes->PendingRequests[Index].Promise); Meshes->PendingRequests.RemoveAt(Index); }
		return Promise;
	};
	auto DeliverFromWorker = [](TUniquePtr<TPromise<FSWGMeshGenerationResult>> Promise, const FSWGMeshGenerationResult& Result)
	{
		if (!Promise) return;
		Async(EAsyncExecution::ThreadPool, [Promise = MoveTemp(Promise), Result]() { Promise->SetValue(Result); }).Wait();
	};
	auto PumpCompletion = []() { FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread); };
	FSWGMeshGenerationResult Success;
	Success.MeshOrComponent.Set<UStaticMesh*>(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));

	ASWGCell* Cancelled = QueueRoom();
	auto CancelPromise = TakePromise(Cancelled);
	Ship->PortalData.Cells.Reset();
	DeliverFromWorker(MoveTemp(CancelPromise), FSWGMeshGenerationResult()); PumpCompletion();
	TestTrue(TEXT("cancelled room with cleared portal table is destroyed safely"), Cancelled->IsActorBeingDestroyed());
	TestFalse(TEXT("cancelled room leaves building registry"), Ship->Cells.Contains(Cancelled));
	Ship->PortalData = Yacht;

	ASWGCell* Stale = QueueRoom();
	auto StalePromise = TakePromise(Stale); Stale->CellNumber = 999;
	DeliverFromWorker(MoveTemp(StalePromise), Success); PumpCompletion();
	TestTrue(TEXT("stale cell identity cannot index another room"), Stale->IsActorBeingDestroyed());
	TestFalse(TEXT("stale room never installs collision"), Stale->bCollisionReady);

	ASWGCell* Late = QueueRoom();
	auto LatePromise = TakePromise(Late);
	DeliverFromWorker(MoveTemp(LatePromise), Success);
	TWeakObjectPtr<USWGMeshGeneratorSubsystem> WeakMeshes = Meshes;
	TWeakObjectPtr<USWGTreSubsystem> WeakTre = Tre;
	Game->Shutdown(); GEngine->DestroyWorldContext(World); World->SetGameInstance(nullptr);
	Game->RemoveFromRoot(); Game = nullptr; Tre = nullptr; Meshes = nullptr;
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("mesh subsystem collected before queued completion"), WeakMeshes.IsValid());
	TestFalse(TEXT("archive subsystem collected before queued completion"), WeakTre.IsValid());
	TestTrue(TEXT("room and ship outlive subsystems in this fixture"), IsValid(Late) && IsValid(Ship));
	PumpCompletion();
	TestFalse(TEXT("retired completion cannot install collision"), Late->bCollisionReady);
	TestEqual(TEXT("retired completion cannot create room lights"), Late->RoomLights.Num(), 0);
	World->DestroyWorld(false); World->RemoveFromRoot();
	return true;
}
#endif
