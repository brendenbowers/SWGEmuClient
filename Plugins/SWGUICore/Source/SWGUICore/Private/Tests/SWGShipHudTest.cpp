#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "SWGHudWidget.h"
#include "SWGActionBarWidget.h"
#include "SWGConditionWidget.h"
#include "SWGGameLayout.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Objects/Player/SWGPlayer.h"
#include "Objects/World/SWGShip.h"
#include "Components/SWGTangibleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Common/SWGWorldScale.h"
#include "Input/UIActionBindingHandle.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGShipHudTest, "SWG.Space.CharacterHud", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGShipHudTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->InitializeStandalone();
	UWorld* World = Game->GetWorld();
	ON_SCOPE_EXIT { Game->Shutdown(); GEngine->DestroyWorldContext(World); };
	USWGObjectGraphSubsystem* Graph = Game->GetSubsystem<USWGObjectGraphSubsystem>();
	USWGGameLayout* PreviousLayout = USWGGameLayout::GetLayout(nullptr);
	ON_SCOPE_EXIT { USWGGameLayout::SetLayout(PreviousLayout); };
	UClass* LayoutClass = LoadClass<USWGGameLayout>(nullptr, TEXT("/SWGUICore/UI/WBP_ClientShell.WBP_ClientShell_C"));
	USWGGameLayout* DetachedLayout = LayoutClass ? CreateWidget<USWGGameLayout>(Game, LayoutClass) : nullptr;
	if (!TestNotNull(TEXT("real client layout loads"), DetachedLayout)) return false;
	USWGGameLayout::SetLayout(DetachedLayout);
	bool bCreated = false;
	TestNull(TEXT("detached cached shell cannot substitute for an attached layout during travel"), USWGGameLayout::GetOrCreate(nullptr, LayoutClass, bCreated));
	UClass* HudClass = LoadClass<USWGHudWidget>(nullptr, TEXT("/SWGUICore/UI/HUD/WBP_Hud.WBP_Hud_C"));
	USWGHudWidget* Hud = HudClass ? CreateWidget<USWGHudWidget>(Game, HudClass) : nullptr;
	if (!Graph || !TestNotNull(TEXT("real character HUD loads"), Hud)) return false;
	const TSharedRef<SWidget> Slate = Hud->TakeWidget();
	const TOptional<FUIInputConfig> InputConfig = Hud->GetDesiredInputConfig();
	if (!TestTrue(TEXT("gameplay HUD declares input ownership"), InputConfig.IsSet())) return false;
	TestEqual(TEXT("HUD keeps game and UI input available"), InputConfig->GetInputMode(), ECommonInputMode::All);
	TestEqual(TEXT("restored HUD recaptures mouse for right-button look"), InputConfig->GetMouseCaptureMode(), EMouseCaptureMode::CaptureDuringRightMouseDown);
	TestFalse(TEXT("restored HUD does not lock camera input"), InputConfig->bIgnoreLookInput);

	ON_SCOPE_EXIT { Hud->NativeDestruct(); };
	if (!TestNotNull(TEXT("real HAM panel is bound"), Hud->ConditionPanel.Get())
		|| !TestNotNull(TEXT("real action bar is bound"), Hud->ActionBar.Get())) return false;
	if (!TestNotNull(TEXT("flight panel is constructed"), Hud->ShipFlightPanel.Get())) return false;
	Graph->LocalPlayerObjectId = 103;
	Graph->bSpaceScene = true;
	for (ESWGContainmentType Type : {ESWGContainmentType::ShipPilot, ESWGContainmentType::ShipPilotPOB})
	{
		Graph->ContainmentTypeByObjectId.Add(103, int32(Type));
		Hud->UpdateFlightReadout();
		TestEqual(TEXT("piloting hides HAM even before ship readiness"), Hud->ConditionPanel->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("piloting hides character actions"), Hud->ActionBar->GetVisibility(), ESlateVisibility::Collapsed);
		TestFalse(TEXT("hidden character actions cannot be triggered by hotkey"), Hud->TriggerActionSlot(0));
		TestTrue(TEXT("pilot sees preparation status while loading"), Hud->ShipFlightPanel->IsVisible());
		TestFalse(TEXT("loading does not display stale speed gauge"), Hud->ShipSpeedGauge->IsVisible());
	}
	ASWGShip* Ship = World->SpawnActor<ASWGShip>();
	ASWGPlayer* Pilot = World->SpawnActor<ASWGPlayer>();
	if (!TestNotNull(TEXT("HUD ship fixture"), Ship) || !TestNotNull(TEXT("HUD pilot fixture"), Pilot)) return false;
	Graph->RegisterStaticObject(100, Ship);
	Graph->RegisterStaticObject(103, Pilot, 100);
	Ship->bHasFlightBaseline = true;
	Ship->TangibleComponent->bHasBase3 = true;
	Ship->ShipState.Flight.MaxSpeed = 100;
	Ship->SpaceVelocity = FVector(50 * SWGWorldScale, 0, 0);
	UStaticMeshComponent* Hull = NewObject<UStaticMeshComponent>(Ship);
	Hull->SetupAttachment(Ship->GetRootComponent());
	Hull->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Hull->RegisterComponent();
	if (!TestTrue(TEXT("HUD fixture reaches actual flight readiness"), Graph->IsLocalSpaceSceneReady())) return false;
	Hud->HandleInputMethodChanged(ECommonInputType::MouseAndKeyboard);
	TestTrue(TEXT("ready pilot sees flight gauges"), Hud->ShipSpeedGauge->IsVisible());
	TestEqual(TEXT("speed gauge uses meters and baseline maximum"), Hud->ShipSpeedGauge->GetPercent(), .5f);
	TestEqual(TEXT("throttle remains independent of actual speed"), Hud->ShipThrottleGauge->GetPercent(), Pilot->GetShipThrottle());
	TestTrue(TEXT("HUD reports pilot camera mode"), Hud->ShipFlightText->GetText().ToString().Contains(TEXT("Cockpit")));
	TestTrue(TEXT("keyboard hints describe leaving seat"), Hud->ShipControlsText->GetText().ToString().Contains(TEXT("X: Leave seat")));
	auto Capture = [&](const TCHAR* Device)
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("SWGCaptureFlightHud"))) return;
		FWidgetRenderer Renderer(false);
		UTextureRenderTarget2D* Target = Renderer.DrawWidget(Slate, FVector2D(996, 452));
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation");
		IFileManager::Get().MakeDirectory(*Directory, true);
		TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*(Directory / FString::Printf(TEXT("SpaceFlightHud-%s.png"), Device))));
		TestTrue(TEXT("flight HUD preview exports"), Target && Writer && FImageUtils::ExportRenderTarget2DAsPNG(Target, *Writer));
	};
	Capture(TEXT("Keyboard"));
	Hud->HandleInputMethodChanged(ECommonInputType::Gamepad);
	const FString PadHints = Hud->ShipControlsText->GetText().ToString();
	TestTrue(TEXT("controller hints explain pitch and roll"), PadHints.Contains(TEXT("Left stick: Pitch / Roll")));
	TestTrue(TEXT("controller hints explain independent camera"), PadHints.Contains(TEXT("Right stick: Camera")));
	TestFalse(TEXT("controller hints omit keyboard bindings"), PadHints.Contains(TEXT("W / S")));
	Capture(TEXT("Controller"));
	Hud->HandleInputMethodChanged(ECommonInputType::MouseAndKeyboard);
	TestFalse(TEXT("switching back removes controller hints"), Hud->ShipControlsText->GetText().ToString().Contains(TEXT("Right stick")));
	Ship->SpaceVelocity = FVector(150 * SWGWorldScale, 0, 0);
	Hud->UpdateFlightReadout();
	TestEqual(TEXT("overspeed gauge remains bounded"), Hud->ShipSpeedGauge->GetPercent(), 1.f);
	Ship->ShipState.Flight.MaxSpeed = 0;
	Hud->UpdateFlightReadout();
	TestEqual(TEXT("unavailable maximum never divides by zero"), Hud->ShipSpeedGauge->GetPercent(), 0.f);
	Ship->bHasFlightBaseline = false;
	Hud->UpdateFlightReadout();
	TestFalse(TEXT("losing readiness hides old gauges"), Hud->ShipSpeedGauge->IsVisible());
	Graph->ContainmentTypeByObjectId.Add(103, int32(ESWGContainmentType::VolumeContained));
	Hud->UpdateFlightReadout();
	TestFalse(TEXT("leaving seat removes flight panel"), Hud->ShipFlightPanel->IsVisible());
	TestTrue(TEXT("walking inside a ship restores HAM"), Hud->ConditionPanel->IsVisible());
	TestTrue(TEXT("walking inside a ship restores character actions"), Hud->ActionBar->IsVisible());
	Graph->ContainmentTypeByObjectId.Add(103, int32(ESWGContainmentType::ShipPilotPOB));
	Graph->bSpaceScene = false;
	Hud->UpdateFlightReadout();
	TestTrue(TEXT("ground return restores HAM despite stale pilot containment"), Hud->ConditionPanel->IsVisible());
	TestTrue(TEXT("ground return restores character actions"), Hud->ActionBar->IsVisible());
	return true;
}
#endif
