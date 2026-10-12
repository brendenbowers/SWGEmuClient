#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "HAL/IConsoleManager.h"
#include "Objects/Player/SWGPlayer.h"
#include "Objects/World/SWGCell.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGStationaryCellUpdateTest, "SWG.Travel.StationaryCellUpdate",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FSWGStationaryCellUpdateTest::RunTest(const FString& Parameters)
{
 UGameInstance* Game = NewObject<UGameInstance>(GEngine);
 Game->InitializeStandalone();
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 if (!World) { Game->Shutdown(); return false; }
 Game->GetWorldContext()->SetCurrentWorld(World);
 World->SetGameInstance(Game);
 ON_SCOPE_EXIT { Game->Shutdown(); World->DestroyWorld(false); };
 ASWGPlayer* Player = World->SpawnActor<ASWGPlayer>();
 APlayerController* Controller = World->SpawnActor<APlayerController>();
 ASWGCell* Cell = World->SpawnActor<ASWGCell>();
 if (!Player || !Controller || !Cell) return false;
 Player->DispatchBeginPlay();
 Cell->DispatchBeginPlay();
 Controller->SetAsLocalPlayerController();
 Controller->Possess(Player);
 if (!TestTrue(TEXT("test pawn is locally controlled"), Player->IsLocallyControlled())) return false;
 Player->SetSceneLoadingLocked(false);
 Cell->SetObjectId(42);
 Player->Tick(0.01f);
 TestEqual(TEXT("stationary exterior has no cell"), Player->LastReportedParentId, int64(0));
 const int32 Before = Player->TransformMovementCounter;
 UBoxComponent* Trigger = NewObject<UBoxComponent>(Cell);
 Trigger->SetupAttachment(Cell->GetRootComponent());
 Trigger->SetBoxExtent(FVector(1000));
 Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
 Trigger->SetCollisionObjectType(ECC_WorldDynamic);
 Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
 Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
 Trigger->RegisterComponent();
 Cell->TriggerVolume = Trigger;
 // This unit world does not tick physics; deliver the room-overlap notification directly.
 Player->GetCapsuleComponent()->BeginComponentOverlap(FOverlapInfo(Trigger), false);
 if (!TestTrue(TEXT("late room trigger detects the stationary player"), Player->ResolveCurrentCell(Player) == Cell)) return false;
 Player->Tick(0.01f);
 TestEqual(TEXT("stationary entry reports the cell immediately"), Player->LastReportedParentId, int64(42));
 TestEqual(TEXT("cell entry retains ambient skylight fill"), Player->FollowCamera->PostProcessSettings.LumenSkylightLeaking,
  FMath::Clamp(IConsoleManager::Get().FindConsoleVariable(TEXT("swg.SkylightLeaking"))->GetFloat(), 0.f, 1.f));
 TestEqual(TEXT("entry sends one transform"), Player->TransformMovementCounter, Before + 1);
 Player->Tick(0.01f);
 TestEqual(TEXT("unchanged stationary cell does not flood updates"), Player->TransformMovementCounter, Before + 1);
 Player->GetCapsuleComponent()->EndComponentOverlap(FOverlapInfo(Trigger), false);
 Player->Tick(0.01f);
 TestEqual(TEXT("stationary exit reports world containment"), Player->LastReportedParentId, int64(0));
 TestEqual(TEXT("exit restores outdoor skylight leakage"), Player->FollowCamera->PostProcessSettings.LumenSkylightLeaking,
  FMath::Clamp(IConsoleManager::Get().FindConsoleVariable(TEXT("swg.SkylightLeaking"))->GetFloat(), 0.f, 1.f));
 TestEqual(TEXT("exit sends one transform"), Player->TransformMovementCounter, Before + 2);
 return true;
}
#endif
