#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "SWGCoreUISubsystem.h"
#include "SWGCoreUISettings.h"
#include "SWGGameLayout.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWGLoadingScreenTest, "SWG.UI.LoadingScreenTravel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSWGLoadingScreenTest::RunTest(const FString& Parameters)
{
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->InitializeStandalone();
	UWorld* World = Game->GetWorld();
	ON_SCOPE_EXIT { Game->Shutdown(); GEngine->DestroyWorldContext(World); };
	USWGClientFlowSubsystem* Flow = Game->GetSubsystem<USWGClientFlowSubsystem>();
	if (!TestNotNull(TEXT("client flow exists"), Flow)) return false;
	// Use inert states so this UI regression needs neither a server nor level travel.
	Flow->RegisterState(ESWGClientState::ZoneLoading, MakeShared<ISWGFlowState>());
	Flow->RegisterState(ESWGClientState::InWorld, MakeShared<ISWGFlowState>());
	Flow->TransitionTo(ESWGClientState::ZoneLoading);
	USWGGameLayout* PreviousLayout = USWGGameLayout::GetLayout(nullptr);
	ON_SCOPE_EXIT { USWGGameLayout::SetLayout(PreviousLayout); };
	USWGGameLayout* Layout = CreateWidget<USWGGameLayout>(Game, USWGCoreUISettings::Get().LayoutClass.LoadSynchronous());
	if (!TestNotNull(TEXT("configured client shell loads"), Layout)) return false;
	const TSharedRef<SWidget> Slate = Layout->TakeWidget();
	USWGGameLayout::SetLayout(Layout);
	UCommonActivatableWidgetStack* LoadingLayer = Cast<UCommonActivatableWidgetStack>(Layout->WidgetTree->FindWidget(TEXT("LoadingLayer")));
	if (!TestNotNull(TEXT("loading layer is bound"), LoadingLayer)) return false;
	// Exercise state restoration independently of the configured fade animation.
	LoadingLayer->SetTransitionDuration(0.f);
	ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
	USWGCoreUISubsystem* UI = NewObject<USWGCoreUISubsystem>(Player);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("new controller fixture"), Controller)) return false;
	TestEqual(TEXT("replacement shell starts empty"), LoadingLayer->GetNumWidgets(), 0);
	UI->PlayerControllerChanged(Controller);
	TestEqual(TEXT("controller replacement restores configured loading screen"), LoadingLayer->GetNumWidgets(), 1);
	UI->PlayerControllerChanged(Controller);
	TestEqual(TEXT("repeated callback keeps one loading screen"), LoadingLayer->GetNumWidgets(), 1);
	FScriptDelegate StateChanged;
	StateChanged.BindUFunction(UI, TEXT("HandleStateChanged"));
	Flow->OnStateChanged.Add(StateChanged);
	ON_SCOPE_EXIT { Flow->OnStateChanged.Remove(StateChanged); };
	Flow->TransitionTo(ESWGClientState::InWorld);
	TestEqual(TEXT("world readiness dismisses loading screen"), LoadingLayer->GetNumWidgets(), 0);
	UI->PlayerControllerChanged(Controller);
	TestEqual(TEXT("ready world does not restore loading screen"), LoadingLayer->GetNumWidgets(), 0);
	return true;
}
#endif
