#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Subsystems/SWGTerrainFloraSubsystem.h"
#include "UObject/GarbageCollection.h"
#include <atomic>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGSpaceFloraLifetimeTest, "SWG.Space.FloraWorkerLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGSpaceFloraLifetimeTest::RunTest(const FString& Parameters)
{
	for (bool bShutdown : {false, true})
	{
		UGameInstance* Game = NewObject<UGameInstance>(GEngine);
		Game->AddToRoot(); Game->InitializeStandalone();
		UWorld* World = Game->GetWorld();
		if (!World) { Game->Shutdown(); Game->RemoveFromRoot(); return false; }
		World->AddToRoot();
		USWGTerrainSubsystem* Terrain = Game->GetSubsystem<USWGTerrainSubsystem>();
		USWGTerrainFloraSubsystem* Flora = Game->GetSubsystem<USWGTerrainFloraSubsystem>();
		FEvent* Entered = FPlatformProcess::GetSynchEventFromPool(true);
		FEvent* Resume = FPlatformProcess::GetSynchEventFromPool(true);
		TFuture<void> Placement;
		bool bCleaned = false;
		ON_SCOPE_EXIT
		{
			Resume->Trigger();
			if (Placement.IsValid()) Placement.Wait();
			if (!bCleaned)
			{
				Game->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
				World->RemoveFromRoot(); Game->RemoveFromRoot();
			}
			FPlatformProcess::ReturnSynchEventToPool(Entered);
			FPlatformProcess::ReturnSynchEventToPool(Resume);
		};
		if (!Terrain || !Flora) return false;
		// Existing FloraPlacement's dense cell: actual placer, 64 plants, no mesh asset required.
		TSharedPtr<FSWGTerrainData, ESPMode::ThreadSafe> Data = MakeShared<FSWGTerrainData, ESPMode::ThreadSafe>();
		Data->Header.FloraCollidable.TileSize = 16.f;
		FSWGFloraFamily& Family = Data->FloraFamilies.AddDefaulted_GetRef();
		Family.FamilyId = 1; Family.Children.AddDefaulted();
		FSWGTerrainLayer& Layer = Data->TopLevelLayers.AddDefaulted_GetRef(); Layer.bEnabled = true;
		FSWGTerrainAffector& Affector = Layer.Affectors.AddDefaulted_GetRef();
		Affector.bEnabled = true; Affector.Type = ESWGTerrainAffectorType::FloraCollidable;
		Affector.FloraFamilyId = 1; Affector.FloraOperation = 1;
		Terrain->PlanetData = Data;
		Flora->OnTerrainLoaded();
		std::atomic<int32> Produced{0};
		Flora->BeforePlacementForTest = [Entered, Resume] { Entered->Trigger(); Resume->Wait(); };
		Flora->AfterPlacementForTest = [&Produced](int32 Count) { Produced.store(Count); };
		const USWGTerrainFloraSubsystem::FCellKey Key{ESWGTerrainFloraTier::Collidable, FIntPoint(-1, 0)};
		Flora->Cells.Add(Key).WantedEditVersion = 37;
		Placement = Flora->StartCellPlacement(Key);
		if (!TestTrue(TEXT("actual placement worker is active"), Entered->Wait(5000))) return false;
		if (!bShutdown)
		{
			Flora->OnZoneReset();
			TestFalse(TEXT("reset stops flora ticking"), Flora->IsTickable());
			TestNull(TEXT("reset removes old flora actor"), Flora->FloraActor.Get());
			Flora->OnTerrainLoaded();
			Flora->Cells.Add(Key).WantedEditVersion = 99;
			Resume->Trigger(); Placement.Wait();
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			TestEqual(TEXT("old placement cannot overwrite a reused cell"), Flora->Cells.FindChecked(Key).PlacedEditVersion, -1);
			TestEqual(TEXT("stale placement releases the flight count"), Flora->PlacementsInFlight, 0);
			Placement = Flora->StartCellPlacement(Key); Placement.Wait();
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			TestEqual(TEXT("current generation still places normally"), Flora->Cells.FindChecked(Key).PlacedEditVersion, 99);
		}
		else
		{
			TWeakObjectPtr<USWGTerrainFloraSubsystem> WeakFlora(Flora);
			TWeakPtr<FSWGTerrainData, ESPMode::ThreadSafe> WeakData(Data);
			Game->Shutdown();
			TestFalse(TEXT("deinitialized flora cannot tick"), Flora->IsTickable());
			GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
			World->RemoveFromRoot(); Game->RemoveFromRoot(); bCleaned = true;
			Data.Reset(); Terrain = nullptr; Flora = nullptr; World = nullptr; Game = nullptr;
			CollectGarbage(RF_NoFlags);
			TestFalse(TEXT("flora owner is collected while its worker is active"), WeakFlora.IsValid());
			TestTrue(TEXT("worker retains its immutable snapshot after owner GC"), WeakData.IsValid());
			Resume->Trigger(); Placement.Wait();
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			// Async signals its future before destroying the callable that owns the snapshot.
			for (int32 Attempt = 0; WeakData.IsValid() && Attempt < 100; ++Attempt) FPlatformProcess::SleepNoStats(.001f);
			TestFalse(TEXT("completed worker releases its snapshot"), WeakData.IsValid());
		}
		TestEqual(TEXT("real placement computes all dense fixture plants"), Produced.load(), 64);
	}
	return true;
}
#endif
