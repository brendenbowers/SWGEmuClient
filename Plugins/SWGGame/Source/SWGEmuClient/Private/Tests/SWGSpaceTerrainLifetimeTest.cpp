#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "UObject/GarbageCollection.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceTerrainLifetimeTest, "SWG.Space.TerrainWorkerLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGSpaceTerrainLifetimeTest::RunTest(const FString& Parameters)
{
	for (bool bShutdown : {false, true})
	{
		UGameInstance* Game = NewObject<UGameInstance>(GEngine);
		Game->AddToRoot(); Game->InitializeStandalone();
		UWorld* World = Game->GetWorld();
		if (!World) { Game->Shutdown(); Game->RemoveFromRoot(); return false; }
		World->AddToRoot();
		USWGTerrainSubsystem* Terrain = Game->GetSubsystem<USWGTerrainSubsystem>();
		USWGTreSubsystem* Tre = Game->GetSubsystem<USWGTreSubsystem>();
		FEvent* Entered = FPlatformProcess::GetSynchEventFromPool(true);
		FEvent* Resume = FPlatformProcess::GetSynchEventFromPool(true);
		bool bCleaned = false;
		ON_SCOPE_EXIT
		{
			Resume->Trigger();
			if (!bCleaned)
			{
				Game->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
				World->RemoveFromRoot(); Game->RemoveFromRoot();
			}
			FPlatformProcess::ReturnSynchEventToPool(Entered);
			FPlatformProcess::ReturnSynchEventToPool(Resume);
		};
		if (!Terrain || !Tre) return false;
		Tre->LoadConfig();
		if (!TestTrue(TEXT("real terrain archives load"), Tre->LoadArchives())) return false;
		int32 Loaded = 0;
		Terrain->OnTerrainLoaded.AddLambda([&Loaded] { ++Loaded; });
		Terrain->BeforeTerrainParseForTest = [Entered, Resume] { Entered->Trigger(); Resume->Wait(); };
		Terrain->BeginLoadTerrain(TEXT("terrain/rori.trn"), FVector::ZeroVector);
		if (!TestTrue(TEXT("actual terrain parser worker is active"), Entered->Wait(5000))) return false;
		const int32 FirstGeneration = Terrain->TerrainGeneration;
		bool bDrainedWithArchivesOpen = false;
		Terrain->BeforeWorkerDrainForTest = [&]
		{
			bDrainedWithArchivesOpen = Tre->IsLoaded();
			Resume->Trigger();
		};
		if (!bShutdown)
		{
			Terrain->BeginLoadTerrain(TEXT("terrain/rori.trn"), FVector::ZeroVector);
			TestTrue(TEXT("same-planet reload advances generation"), Terrain->TerrainGeneration != FirstGeneration);
			for (TFuture<void>& Task : Terrain->WorkerTasks) Task.Wait();
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			TestEqual(TEXT("only the current same-planet parse publishes"), Loaded, 1);
			TestTrue(TEXT("current terrain parse is retained"), Terrain->GetPlanetData().IsValid());
		}
		else
		{
			TWeakObjectPtr<USWGTerrainSubsystem> WeakTerrain(Terrain);
			Tre->Deinitialize();
			TestTrue(TEXT("archive teardown stops terrain consumer"), Terrain->bIsShuttingDown);
			TestEqual(TEXT("archive teardown drains actual terrain workers"), Terrain->WorkerTasks.Num(), 0);
			TestFalse(TEXT("terrain no longer ticks"), Terrain->IsTickable());
			TestFalse(TEXT("archives close after worker drain"), Tre->IsLoaded());
			Game->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
			World->RemoveFromRoot(); Game->RemoveFromRoot(); bCleaned = true;
			Terrain = nullptr; Tre = nullptr; World = nullptr; Game = nullptr;
			CollectGarbage(RF_NoFlags);
			TestFalse(TEXT("terrain owner is collected before its queued completion"), WeakTerrain.IsValid());
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			TestEqual(TEXT("late completion cannot publish after shutdown and GC"), Loaded, 0);
		}
		TestTrue(TEXT("workers drain while archives are still readable"), bDrainedWithArchivesOpen);
	}
	return true;
}
#endif
