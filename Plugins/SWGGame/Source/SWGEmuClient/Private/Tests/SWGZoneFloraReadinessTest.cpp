#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Objects/Creature/SWGCreature.h"
#include "Common/SWGWorldScale.h"
#include "Subsystems/SWGTerrainFloraSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGZoneFloraReadinessTest, "SWG.Flow.ZoneFloraReadiness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGZoneFloraReadinessTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Flora"));
	IConsoleVariable* Collidable = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.FloraCollidable"));
	IConsoleVariable* Scale = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.FloraDistanceScale"));
	const int32 WasEnabled = Enabled->GetInt(), WasCollidable = Collidable->GetInt();
	const float WasScale = Scale->GetFloat();
	ON_SCOPE_EXIT
	{
		Enabled->SetWithCurrentPriority(WasEnabled);
		Collidable->SetWithCurrentPriority(WasCollidable);
		Scale->SetWithCurrentPriority(WasScale);
	};
	Enabled->SetWithCurrentPriority(1);
	Collidable->SetWithCurrentPriority(1);
	Scale->SetWithCurrentPriority(1.f);
	UGameInstance* Game = NewObject<UGameInstance>();
	USWGTerrainFloraSubsystem* Flora = NewObject<USWGTerrainFloraSubsystem>(Game);
	TSharedPtr<FSWGTerrainData, ESPMode::ThreadSafe> Planet = MakeShared<FSWGTerrainData, ESPMode::ThreadSafe>();
	Planet->Header.FloraCollidable.MaxDistance = 1024.f;
	Flora->Planet = Planet;
	Flora->bTerrainLoaded = true;
	const float Size = FSWGTerrainFloraPlacer::GetCellSize(*Planet, ESWGTerrainFloraTier::Collidable);
	const FVector2D Center(Size * .5f, Size * .5f);
	TestFalse(TEXT("missing near-spawn flora cells hold the loading screen"), Flora->IsSpawnAreaReady(Center));
	const int32 Radius = FMath::CeilToInt(128.f / Size) + 2;
	for (int32 Y = -Radius; Y <= Radius; ++Y)
		for (int32 X = -Radius; X <= Radius; ++X)
		{
			auto& Cell = Flora->Cells.Add({ESWGTerrainFloraTier::Collidable, FIntPoint(X, Y)});
			Cell.PlacedEditVersion = Cell.WantedEditVersion = 0;
		}
	TestTrue(TEXT("placed nearby cells reveal without waiting for the full 1024 m ring"), Flora->IsSpawnAreaReady(Center));
	auto& Cell = Flora->Cells.FindChecked({ESWGTerrainFloraTier::Collidable, FIntPoint::ZeroValue});
	Cell.bPlaceInFlight = true;
	TestFalse(TEXT("near placement worker keeps screen up"), Flora->IsSpawnAreaReady(Center));
	Cell.bPlaceInFlight = false;
	Cell.WantedEditVersion = 1;
	TestFalse(TEXT("late terrain edits must clear and replace nearby flora"), Flora->IsSpawnAreaReady(Center));
	Cell.PlacedEditVersion = 1;
	auto& Group = Cell.PendingGroups.AddDefaulted_GetRef();
	Group.AppearanceKey = TEXT("appearance/missing-loading-test.apt");
	TestFalse(TEXT("actorless appearances must attach their instance groups before reveal"), Flora->IsSpawnAreaReady(Center));
	Flora->Appearances.Add(Group.AppearanceKey);
	Flora->OnAppearanceMeshReady(Group.AppearanceKey, nullptr, {});
	TestTrue(TEXT("failed appearance drops its instances and releases readiness"), Flora->IsSpawnAreaReady(Center));
	Cell.bPlaceInFlight = true;
	Collidable->SetWithCurrentPriority(0);
	TestTrue(TEXT("disabled tier cannot hold loading screen"), Flora->IsSpawnAreaReady(Center));
	Collidable->SetWithCurrentPriority(1);
	Scale->SetWithCurrentPriority(0.f);
	TestTrue(TEXT("zero vegetation distance cannot hold loading screen"), Flora->IsSpawnAreaReady(Center));
	Scale->SetWithCurrentPriority(1.f);
	Enabled->SetWithCurrentPriority(0);
	TestTrue(TEXT("disabled vegetation cannot hold loading screen"), Flora->IsSpawnAreaReady(Center));

	UGameInstance* StreamingGame = NewObject<UGameInstance>(GEngine);
	StreamingGame->AddToRoot();
	StreamingGame->InitializeStandalone();
	UWorld* World = StreamingGame->GetWorld();
	World->AddToRoot();
	ON_SCOPE_EXIT
	{
		StreamingGame->Shutdown();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		World->RemoveFromRoot();
		StreamingGame->RemoveFromRoot();
	};
	USWGTerrainSubsystem* Terrain = StreamingGame->GetSubsystem<USWGTerrainSubsystem>();
	USWGTerrainFloraSubsystem* StreamingFlora = StreamingGame->GetSubsystem<USWGTerrainFloraSubsystem>();
	if (!TestNotNull(TEXT("terrain fixture"), Terrain) || !TestNotNull(TEXT("flora fixture"), StreamingFlora)) return false;
	Terrain->SpawnRawPosition = FVector2D(2000.f, 3000.f);
	FVector2D StreamingCenter;
	TestTrue(TEXT("loading flora can stream before possession"), StreamingFlora->GetStreamingCenter(StreamingCenter));
	TestEqual(TEXT("unpossessed loading streams the readiness center"), StreamingCenter, Terrain->SpawnRawPosition);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ASWGCreature* Pawn = World->SpawnActor<ASWGCreature>();
	if (!TestNotNull(TEXT("controller fixture"), Controller) || !TestNotNull(TEXT("pawn fixture"), Pawn)) return false;
	World->AddController(Controller);
	Controller->Possess(Pawn);
	if (!TestTrue(TEXT("streaming fixture possesses the pawn"), Controller->GetPawn() == Pawn)) return false;
	Pawn->SetActorLocation(SWGToUnrealSpace(FVector(10.f, 20.f, 0.f)));
	Pawn->bAwaitingCellPlacement = true;
	TestTrue(TEXT("unplaced pawn cannot divert loading flora"), StreamingFlora->GetStreamingCenter(StreamingCenter));
	TestEqual(TEXT("cell-local pawn keeps flora at spawn"), StreamingCenter, Terrain->SpawnRawPosition);
	Pawn->bAwaitingCellPlacement = false;
	Pawn->SetActorLocation(SWGToUnrealSpace(FVector(5000.f, 6000.f, 0.f)));
	StreamingFlora->GetStreamingCenter(StreamingCenter);
	TestEqual(TEXT("pawn correction cannot unload the required spawn flora"), StreamingCenter, Terrain->SpawnRawPosition);
	Terrain->FinishSpawnSceneLoading();
	TestTrue(TEXT("flora follows the placed pawn after reveal"), StreamingFlora->GetStreamingCenter(StreamingCenter));
	TestEqual(TEXT("revealed flora center uses current pawn world position"), StreamingCenter, FVector2D(5000.f, 6000.f));
	return true;
}
#endif
