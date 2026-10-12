#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Common/SWGWorldScale.h"
#include "Subsystems/SWGEffectSubsystem.h"
#include "SWGEffectsSubsystem.h"
#include "SWGParticleEffectComponent.h"
#include "TRE/SWGClientEffectReader.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGObjectEffectPlacementTest,"SWGEmu.Fx.ObjectTransformPlacement",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FSWGObjectEffectPlacementTest::RunTest(const FString& Parameters)
{
 UGameInstance* Game=NewObject<UGameInstance>(GEngine); Game->InitializeStandalone();
 UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
 if(!World) { Game->Shutdown(); return false; }
 Game->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(Game);
 ON_SCOPE_EXIT { Game->Shutdown(); World->DestroyWorld(false); };
 AActor* Actor=World->SpawnActor<AActor>();
 USceneComponent* Root=NewObject<USceneComponent>(Actor); Actor->SetRootComponent(Root); Root->RegisterComponent();
 Actor->SetActorLocationAndRotation(FVector(1000,2000,3000),FRotator(0,90,0));
 const FTransform Local(SWGNativeToUnrealRotation(0,0.70710678f,0,0.70710678f),SWGToUnrealSpace(FVector(1,3,2)));
 TestTrue(TEXT("native Y-up position maps to UE centimetres"),Local.GetLocation().Equals(FVector(300,100,200)));
 USWGEffectsSubsystem* Particles=NewObject<USWGEffectsSubsystem>(World);
 Particles->EffectCache.Add(TEXT("appearance/test.prt"),MakeShared<FSWGParticleEffect>());
 const FSWGEffectHandle Handle=Particles->PlayParticleAttached(TEXT("appearance/test.prt"),Root,NAME_None,1.f,FLinearColor::White,Local);
 if(!TestTrue(TEXT("attached particle created"),Handle.IsValid())) return false;
 const FTransform Expected=Local*Root->GetComponentTransform();
 TestTrue(TEXT("particle local offset rotates with parent"),Handle.Effect->GetComponentLocation().Equals(Expected.GetLocation(),0.01));
 TestTrue(TEXT("particle carries local rotation"),Handle.Effect->GetComponentQuat().Equals(Expected.GetRotation(),0.0001));
 USWGEffectSubsystem* Effects=NewObject<USWGEffectSubsystem>(Game);
 FSWGClientEffectLight Light; Light.Params[0]=1.f; Light.Params[4]=10.f;
 Effects->PlayLight(Light,Root,NAME_None,Expected,Local);
 TInlineComponentArray<UPointLightComponent*> Lights(Actor);
 if(!TestEqual(TEXT("one attached light created"),Lights.Num(),1)) return false;
 TestTrue(TEXT("light uses the same local transform"),Lights[0]->GetComponentLocation().Equals(Expected.GetLocation(),0.01));
 Actor->SetActorLocationAndRotation(FVector(2000,1000,500),FRotator(0,180,0));
 const FTransform Moved=Local*Root->GetComponentTransform();
 TestTrue(TEXT("particle follows moving object"),Handle.Effect->GetComponentLocation().Equals(Moved.GetLocation(),0.01));
 TestTrue(TEXT("light follows moving object"),Lights[0]->GetComponentLocation().Equals(Moved.GetLocation(),0.01));
 TestEqual(TEXT("missing object is skipped"),Effects->PlayEffectOnObjectTransform(TEXT("test"),42,Local),0);
 Particles->StopAll(true);
 return true;
}
#endif
