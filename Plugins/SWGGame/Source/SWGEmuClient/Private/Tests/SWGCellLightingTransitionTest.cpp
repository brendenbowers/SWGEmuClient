#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "HAL/IConsoleManager.h"
#include "Objects/Player/SWGPlayer.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGCellLightingTransitionTest, "SWGEmu.Fx.CellLightingTransition",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FSWGCellLightingTransitionTest::RunTest(const FString& Parameters)
{
 TGuardValue<bool> AllowCallbacks(GAllowActorScriptExecutionInEditor, true);
 UGameInstance* Game = NewObject<UGameInstance>(GEngine); Game->InitializeStandalone();
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 if (!World) { Game->Shutdown(); return false; }
 Game->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(Game);
 ON_SCOPE_EXIT { Game->Shutdown(); World->DestroyWorld(false); };
 ASWGPlayer* Player = World->SpawnActor<ASWGPlayer>(); Player->DispatchBeginPlay();
 ASWGBuilding* Building = World->SpawnActor<ASWGBuilding>();
 ASWGCell* Rooms[2]; UBoxComponent* Triggers[2]; UPointLightComponent* Lights[2];
 for (int32 Index=0; Index<2; ++Index)
 {
  Rooms[Index] = World->SpawnActor<ASWGCell>(); Rooms[Index]->OwningBuilding = Building;
  Rooms[Index]->DispatchBeginPlay();
  Triggers[Index] = NewObject<UBoxComponent>(Rooms[Index]);
  Triggers[Index]->SetupAttachment(Rooms[Index]->GetRootComponent());
  Triggers[Index]->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  Triggers[Index]->SetCollisionObjectType(ECC_WorldDynamic);
  Triggers[Index]->SetCollisionResponseToAllChannels(ECR_Ignore);
  Triggers[Index]->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
  Triggers[Index]->RegisterComponent(); Rooms[Index]->TriggerVolume = Triggers[Index];
  Lights[Index] = NewObject<UPointLightComponent>(Rooms[Index]);
  Lights[Index]->SetIntensity(100.f); Lights[Index]->SetVisibility(false);
  Rooms[Index]->AddRoomLight(Lights[Index]);
 }
 // Deliver physics notifications explicitly in this non-ticking unit world.
 Player->GetCapsuleComponent()->BeginComponentOverlap(FOverlapInfo(Triggers[0]), false);
 Building->RegisterCellTrigger(Rooms[0], true);
 TestTrue(TEXT("late registration lights an already occupied room"), Lights[0]->GetVisibleFlag());
 const float RoomScale = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.RoomLightScale"))->GetFloat();
 TestTrue(TEXT("default room lighting preserves authored brightness"), RoomScale >= 1.f);
 TestEqual(TEXT("room entry applies authored light intensity"), Lights[0]->Intensity, 100.f * RoomScale);
 Building->RegisterCellTrigger(Rooms[0], true);
 Building->RegisterCellTrigger(Rooms[1], true);
 Player->GetCapsuleComponent()->BeginComponentOverlap(FOverlapInfo(Triggers[1]), false);
 Triggers[1]->OnComponentBeginOverlap.Broadcast(Triggers[1], Player, Player->GetCapsuleComponent(), INDEX_NONE, false, FHitResult());
 TestFalse(TEXT("entering next room disables previous lights"), Lights[0]->GetVisibleFlag());
 TestTrue(TEXT("next room lights enabled"), Lights[1]->GetVisibleFlag());
 Player->GetCapsuleComponent()->EndComponentOverlap(FOverlapInfo(Triggers[1]), false);
 Triggers[1]->OnComponentEndOverlap.Broadcast(Triggers[1], Player, Player->GetCapsuleComponent(), INDEX_NONE);
 TestTrue(TEXT("backing out of doorway restores occupied room"), Lights[0]->GetVisibleFlag());
 TestEqual(TEXT("re-entry does not compound the intensity multiplier"), Lights[0]->Intensity, 100.f * RoomScale);
 TestFalse(TEXT("departed room lights disabled"), Lights[1]->GetVisibleFlag());
 Player->GetCapsuleComponent()->EndComponentOverlap(FOverlapInfo(Triggers[0]), false);
 Triggers[0]->OnComponentEndOverlap.Broadcast(Triggers[0], Player, Player->GetCapsuleComponent(), INDEX_NONE);
 TestFalse(TEXT("exit disables interior lights"), Lights[0]->GetVisibleFlag());
 TestFalse(TEXT("duplicate registration did not corrupt occupancy"), Building->IsPlayerInside());
 return true;
}
#endif
