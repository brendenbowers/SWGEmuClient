#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "Components/PointLightComponent.h"
#include "Components/SWGPropAttachmentsComponent.h"
#include "Subsystems/SWGPropAttachmentsSubsystem.h"
#include "Common/SWGRetailLight.h"
#include "Engine/GameInstance.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGNaturalLightTest, "SWGEmu.Fx.NaturalLighting",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FSWGNaturalLightTest::RunTest(const FString& Parameters)
{
 IConsoleVariable* Mode = IConsoleManager::Get().FindConsoleVariable(TEXT("swg.Fx.PropLightRetailFalloff"));
 if (!Mode) return false;
 const int32 Previous = Mode->GetInt();
 ON_SCOPE_EXIT { Mode->Set(Previous, ECVF_SetByCode); };
 Mode->Set(0, ECVF_SetByCode);
 UPointLightComponent* Light = NewObject<UPointLightComponent>();
 SWGRetailLight::ApplyFalloff(*Light, 2.f);
 TestTrue(TEXT("native physical falloff enabled"), bool(Light->bUseInverseSquaredFalloff));
 TestTrue(TEXT("physical units retained"), Light->IntensityUnits == ELightUnits::Candelas);
 TestTrue(TEXT("finite light source softens highlights"), Light->SourceRadius > 0.f);
 TestEqual(TEXT("flashing lights do not illuminate fog"), Light->VolumetricScatteringIntensity, 0.f);
 TestEqual(TEXT("physical brightness does not grow with the radius"), SWGRetailLight::ComputeIntensity(2.f, 500.f, 1000.f), 500.f);
 UGameInstance* Game = NewObject<UGameInstance>();
 USWGPropAttachmentsSubsystem* Subsystem = NewObject<USWGPropAttachmentsSubsystem>(Game);
 USWGPropAttachmentsComponent* Prop = NewObject<USWGPropAttachmentsComponent>();
 Prop->OwnerSubsystem = Subsystem;
 FSWGPropLightSlot& Slot = Prop->LightSlots.AddDefaulted_GetRef();
 Slot.Light = Light; Slot.RangeCm = 300.f; Slot.SecondRangeCm = 900.f; Slot.bFlicker = true;
 float Minimum = MAX_flt, Maximum = 0.f;
 for (int32 Frame = 0; Frame < 60; ++Frame)
 {
  Prop->TickFlicker(Frame / 30.f);
  TestEqual(TEXT("flash has a stable attenuation boundary"), Light->AttenuationRadius, 900.f);
  Minimum = FMath::Min(Minimum, Light->Intensity); Maximum = FMath::Max(Maximum, Light->Intensity);
 }
 TestTrue(TEXT("flicker varies actual emitted light"), Maximum > Minimum);
 TestTrue(TEXT("flicker never exceeds the authored peak power"), Maximum <= Subsystem->GetLightIntensityScale());
 Mode->Set(1, ECVF_SetByCode); SWGRetailLight::ApplyFalloff(*Light, 2.f);
 TestFalse(TEXT("legacy falloff remains selectable"), bool(Light->bUseInverseSquaredFalloff));
 return true;
}
#endif
