#include "SWGUISubsystem.h"
#include "SWGUISettings.h"
#include "SWGGameLayout.h"
#include "SWGStateTransitionConfig.h"
#include "SWGRadialMenuWidget.h"
#include "SWGSuiBoxWidget.h"
#include "SWGMissionBrowserWidget.h"
#include "SWGMissionBrowserDockWidget.h"
#include "SWGTravelWidget.h"
#include "SWGInventoryWidget.h"
#include "SWGInventoryDockWidget.h"
#include "CommonInputSubsystem.h"
#include "SWGExamineWidget.h"
#include "SWGWaypointListWidget.h"
#include "SWGDatapadWidget.h"
#include "Subsystems/SWGExamineSubsystem.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGMissionSubsystem.h"
#include "Subsystems/SWGTravelSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"

void USWGUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UGameInstance* GameInstance = GetLocalPlayer()->GetGameInstance();
	if (USWGClientFlowSubsystem* Flow = GameInstance->GetSubsystem<USWGClientFlowSubsystem>())
	{
		Flow->OnStateChanged.AddDynamic(this, &USWGUISubsystem::HandleStateChanged);
	}
	if (USWGRadialMenuSubsystem* Radial = GameInstance->GetSubsystem<USWGRadialMenuSubsystem>())
	{
		Radial->OnMenuReceived.AddDynamic(this, &USWGUISubsystem::HandleRadialMenuReceived);
	}
	if (USWGSuiSubsystem* Sui = GameInstance->GetSubsystem<USWGSuiSubsystem>())
	{
		Sui->OnPageOpened.AddDynamic(this, &USWGUISubsystem::HandleSuiPageOpened);
		Sui->OnPageClosed.AddDynamic(this, &USWGUISubsystem::HandleSuiPageClosed);
	}
	if (USWGExamineSubsystem* Examine = GameInstance->GetSubsystem<USWGExamineSubsystem>())
	{
		Examine->OnExamineRequested.AddDynamic(this, &USWGUISubsystem::HandleExamineRequested);
	}
	if (USWGMissionSubsystem* Missions = GameInstance->GetSubsystem<USWGMissionSubsystem>())
	{
		Missions->OnMissionWindowRequested.AddDynamic(this, &USWGUISubsystem::HandleMissionWindowRequested);
		Missions->OnMissionListChanged.AddDynamic(this, &USWGUISubsystem::HandleMissionListChanged);
	}
	if (USWGTravelSubsystem* Travel = GameInstance->GetSubsystem<USWGTravelSubsystem>())
	{
		Travel->OnTravelWindowRequested.AddDynamic(this, &USWGUISubsystem::HandleTravelWindowRequested);
	}
	if (UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(GetLocalPlayer()))
	{
		InputMethodChangedHandle = CommonInput->OnInputMethodChangedNative.AddUObject(this, &USWGUISubsystem::HandleInputMethodChanged);
	}
}

void USWGUISubsystem::Deinitialize()
{
	if (UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(GetLocalPlayer()))
	{
		CommonInput->OnInputMethodChangedNative.Remove(InputMethodChangedHandle);
	}
	if (UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr)
	{
		if (USWGClientFlowSubsystem* Flow = GameInstance->GetSubsystem<USWGClientFlowSubsystem>())
		{
			Flow->OnStateChanged.RemoveAll(this);
		}
		if (USWGRadialMenuSubsystem* Radial = GameInstance->GetSubsystem<USWGRadialMenuSubsystem>())
		{
			Radial->OnMenuReceived.RemoveAll(this);
		}
		if (USWGSuiSubsystem* Sui = GameInstance->GetSubsystem<USWGSuiSubsystem>())
		{
			Sui->OnPageOpened.RemoveAll(this);
			Sui->OnPageClosed.RemoveAll(this);
		}
		if (USWGExamineSubsystem* Examine = GameInstance->GetSubsystem<USWGExamineSubsystem>())
		{
			Examine->OnExamineRequested.RemoveAll(this);
		}
		if (USWGMissionSubsystem* Missions = GameInstance->GetSubsystem<USWGMissionSubsystem>())
		{
			Missions->OnMissionWindowRequested.RemoveAll(this);
			Missions->OnMissionListChanged.RemoveAll(this);
		}
		if (USWGTravelSubsystem* Travel = GameInstance->GetSubsystem<USWGTravelSubsystem>())
		{
			Travel->OnTravelWindowRequested.RemoveAll(this);
		}
	}

	Super::Deinitialize();
}

void USWGUISubsystem::HandleRadialMenuReceived(const FSWGRadialMenu& Menu)
{
	APlayerController* PlayerController = GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
	if (!PlayerController || Menu.Items.IsEmpty())
	{
		return;
	}

	if (RadialMenu)
	{
		RadialMenu->Close();
		RadialMenu = nullptr;
	}

	TSubclassOf<USWGRadialMenuWidget> MenuClass = USWGUISettings::Get().RadialMenuClass.LoadSynchronous();
	if (!MenuClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: no RadialMenuClass set in Project Settings > SWG UI"));
		return;
	}

	RadialMenu = CreateWidget<USWGRadialMenuWidget>(PlayerController, MenuClass);
	if (RadialMenu)
	{
		// Above the layout (which sits at 100) so it draws over the HUD.
		RadialMenu->AddToPlayerScreen(200);
		RadialMenu->OnClosed.AddUObject(this, &USWGUISubsystem::HandleRadialMenuClosed);
		RadialMenu->Open(Menu);
	}
	UE_LOG(LogTemp, Log, TEXT("USWGUISubsystem: radial menu for %lld at (%.0f, %.0f) -> %s"),
		Menu.ObjectId, Menu.ScreenPosition.X, Menu.ScreenPosition.Y, RadialMenu ? *RadialMenu->GetName() : TEXT("FAILED"));
}

void USWGUISubsystem::HandleSuiPageOpened(const FSWGSuiPage& Page)
{
	USWGGameLayout* Layout = EnsureLayout();
	TSubclassOf<USWGSuiBoxWidget> BoxClass = USWGUISettings::Get().SuiBoxClass.LoadSynchronous();
	if (!Layout || !BoxClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: SUI page %d (%s) dropped — %s"), Page.PageId, *Page.ScriptClass,
			Layout ? TEXT("no SuiBoxClass set in Project Settings > SWG UI") : TEXT("no layout"));
		return;
	}

	// An update to an open page re-fills its window in place.
	if (TObjectPtr<USWGSuiBoxWidget>* Existing = SuiWindows.Find(Page.PageId); Existing && *Existing)
	{
		(*Existing)->SetPage(Page);
		return;
	}

	USWGSuiBoxWidget* Window = Cast<USWGSuiBoxWidget>(Layout->PushWidgetToLayerStack(USWGGameLayout::TAG_Layer_Modal, BoxClass));
	if (Window)
	{
		Window->SetPage(Page);
		SuiWindows.Add(Page.PageId, Window);
	}
}

void USWGUISubsystem::HandleSuiPageClosed(int32 PageId)
{
	TObjectPtr<USWGSuiBoxWidget> Window;
	if (SuiWindows.RemoveAndCopyValue(PageId, Window) && Window && Window->IsActivated())
	{
		Window->DeactivateWidget();
	}
}

void USWGUISubsystem::HandleMissionWindowRequested(int64 TerminalObjectId)
{
	if (IsGamepadActive())
	{
		if (MissionWindow)
		{
			MissionWindow->Close();
		}
		if (MissionDock)
		{
			MissionDock->Close();
		}
		if (!InventoryDock)
		{
			OpenInventory(true);
		}
		if (InventoryDock)
		{
			UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
			USWGMissionSubsystem* Missions = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr;
			InventoryDock->SetMissions(Missions ? Missions->GetMissions() : TArray<FSWGMissionEntry>());
			InventoryDock->SetTab(ESWGInventoryTab::Missions);
			InventoryDock->Refocus();
		}
		return;
	}

	if (MissionWindow || MissionDock)
	{
		// Already up (re-used the terminal, or a second terminal) — raise it and let HandleMissionListChanged refresh it.
		if (MissionWindow)
		{
			HandleWindowPressed(MissionWindow);
		}
		HandleMissionListChanged();
		return;
	}

	APlayerController* PlayerController = GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
	if (!PlayerController)
	{
		return;
	}

	TSubclassOf<USWGMissionBrowserWidget> BrowserClass = USWGUISettings::Get().MissionBrowserClass.LoadSynchronous();
	if (!BrowserClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: mission window for %lld dropped — no MissionBrowserClass set in Project Settings > SWG UI"), TerminalObjectId);
		return;
	}

	MissionWindow = CreateWidget<USWGMissionBrowserWidget>(PlayerController, BrowserClass);
	ShowWindow(MissionWindow);
	MissionWindow->CenterOnScreen();
	HandleMissionListChanged();
}

void USWGUISubsystem::HandleMissionDockClosed()
{
	MissionDock = nullptr;
}

void USWGUISubsystem::HandleMissionListChanged()
{
	UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
	USWGMissionSubsystem* Missions = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr;
	if (!Missions)
	{
		return;
	}

	if (MissionWindow)
	{
		MissionWindow->SetMissions(Missions->GetMissions());
	}
	if (MissionDock)
	{
		MissionDock->SetMissions(Missions->GetMissions());
	}
	if (InventoryDock)
	{
		InventoryDock->SetMissions(Missions->GetMissions());
	}
}

void USWGUISubsystem::HandleTravelWindowRequested()
{
	if (TravelWindow)
	{
		TravelWindow->SetControllerMode(IsGamepadActive());
		HandleWindowPressed(TravelWindow);
		return;
	}

	APlayerController* PlayerController = GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
	if (!PlayerController)
	{
		return;
	}
	TravelWindow = CreateWidget<USWGTravelWidget>(PlayerController, USWGTravelWidget::StaticClass());
	TravelWindow->SetControllerMode(IsGamepadActive());
	ShowWindow(TravelWindow);
	TravelWindow->CenterOnScreen();
}

void USWGUISubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);

	// Runs from SetPlayer() during SpawnPlayActor, so the layout exists before
	// the controller's BeginPlay kicks off the flow's first transition.
	if (NewPlayerController)
	{
		EnsureLayout();
	}
}

USWGGameLayout* USWGUISubsystem::EnsureLayout()
{
	APlayerController* PlayerController = GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
	if (!PlayerController)
	{
		return USWGGameLayout::GetLayout(nullptr);
	}

	TSubclassOf<USWGGameLayout> LayoutClass = USWGUISettings::Get().LayoutClass.LoadSynchronous();
	if (!LayoutClass)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGUISubsystem: no LayoutClass set in Project Settings > SWG UI"));
		return nullptr;
	}

	bool bCreated = false;
	return USWGGameLayout::GetOrCreate(PlayerController, LayoutClass, bCreated);
}

void USWGUISubsystem::HandleStateChanged(ESWGClientState OldState, ESWGClientState NewState)
{
	USWGGameLayout* Layout = EnsureLayout();
	if (!Layout)
	{
		return;
	}

	// A scene change from in world (teleport, zone travel) wants the same
	// loading screen as the one from character select.
	const ESWGClientState RowOldState = (OldState == ESWGClientState::InWorld && NewState == ESWGClientState::ZoneLoading)
		? ESWGClientState::CharacterSelected : OldState;

	if (const UDataTable* Table = USWGUISettings::Get().StateTransitionTable.LoadSynchronous())
	{
		for (const auto& Row : Table->GetRowMap())
		{
			const FSWGStateTransitionRow* TransitionRow = reinterpret_cast<const FSWGStateTransitionRow*>(Row.Value);
			if (TransitionRow && TransitionRow->OldState == RowOldState && TransitionRow->NewState == NewState)
			{
				const FGameplayTag Tag = TransitionRow->LayerTag.IsValid() ? TransitionRow->LayerTag : USWGGameLayout::TAG_Layer_Menu;
				Layout->PushWidgetToLayerStack(Tag, TransitionRow->WidgetClass);
				break;
			}
		}
	}

	if (NewState == ESWGClientState::CharacterSelected)
	{
		Layout->ClearLayer(USWGGameLayout::TAG_Layer_Menu);
	}
	else if (NewState == ESWGClientState::InWorld)
	{
		Layout->ClearLayer(USWGGameLayout::TAG_Layer_Menu);
		Layout->ClearLayer(USWGGameLayout::TAG_Layer_Loading);
		Layout->ClearLayer(USWGGameLayout::TAG_Layer_Modal);
	}
}

// ── Floating windows ─────────────────────────────────────────────────────────

void USWGUISubsystem::ShowWindow(USWGWindowWidget* Window)
{
	if (!Window)
	{
		return;
	}
	Windows.AddUnique(Window);
	Window->OnPressed.AddUObject(this, &USWGUISubsystem::HandleWindowPressed);
	Window->OnClosed.AddUObject(this, &USWGUISubsystem::HandleWindowClosed);
	Window->AddToPlayerScreen(NextWindowZ++);
	Window->SetFocus();
}

void USWGUISubsystem::HandleWindowPressed(USWGWindowWidget* Window)
{
	// Raise: the player screen stacks by z-order, and z-order is set on add.
	if (Window && Window->IsInViewport() && Windows.Num() > 1 && Windows.Last() != Window)
	{
		Window->RemoveFromParent();
		Window->AddToPlayerScreen(NextWindowZ++);
		Windows.Remove(Window);
		Windows.Add(Window);
	}
}

void USWGUISubsystem::HandleWindowClosed(USWGWindowWidget* Window)
{
	Windows.Remove(Window);
	if (Window == InventoryWindow)
	{
		InventoryWindow = nullptr;
	}
	if (Window == MissionWindow)
	{
		MissionWindow = nullptr;
	}
	if (Window == TravelWindow)
	{
		TravelWindow = nullptr;
	}
	if (Window == WaypointWindow)
	{
		WaypointWindow = nullptr;
	}
	if (Window == DatapadWindow)
	{
		DatapadWindow = nullptr;
	}
	for (auto It = ExamineWindows.CreateIterator(); It; ++It)
	{
		if (It->Value == Window)
		{
			It.RemoveCurrent();
		}
	}
}

// ── Inventory ────────────────────────────────────────────────────────────────

bool USWGUISubsystem::IsGamepadActive() const
{
	const UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(GetLocalPlayer());
	return CommonInput && CommonInput->GetCurrentInputType() == ECommonInputType::Gamepad;
}

bool USWGUISubsystem::IsInventoryOpen() const
{
	return InventoryWindow != nullptr || InventoryDock != nullptr;
}

void USWGUISubsystem::ToggleInventory()
{
	if (InventoryDock && InventoryDock->GetTab() != ESWGInventoryTab::Equipped
		&& InventoryDock->GetTab() != ESWGInventoryTab::Inventory
		&& InventoryDock->GetTab() != ESWGInventoryTab::Examine)
	{
		InventoryDock->SetTab(ESWGInventoryTab::Inventory);
		InventoryDock->Refocus();
		return;
	}
	if (IsInventoryOpen())
	{
		CloseInventory();
		return;
	}
	OpenInventory(IsGamepadActive());
}

void USWGUISubsystem::CloseInventory()
{
	if (InventoryWindow)
	{
		InventoryWindow->Close();
	}
	if (InventoryDock)
	{
		InventoryDock->Close();
	}
}

void USWGUISubsystem::OpenInventory(bool bDocked)
{
	APlayerController* PlayerController = GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
	if (!PlayerController)
	{
		return;
	}

	if (bDocked)
	{
		TSubclassOf<USWGInventoryDockWidget> DockClass = USWGUISettings::Get().InventoryDockClass.LoadSynchronous();
		if (!DockClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: no InventoryDockClass set in Project Settings > SWG UI"));
			return;
		}
		InventoryDock = CreateWidget<USWGInventoryDockWidget>(PlayerController, DockClass);
		InventoryDock->OnClosed.AddUObject(this, &USWGUISubsystem::HandleInventoryDockClosed);
		// Same band as the floating windows: over the HUD, under the radial menu.
		InventoryDock->AddToPlayerScreen(NextWindowZ++);
		return;
	}

	TSubclassOf<USWGInventoryWidget> InventoryClass = USWGUISettings::Get().InventoryClass.LoadSynchronous();
	if (!InventoryClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: no InventoryClass set in Project Settings > SWG UI"));
		return;
	}
	InventoryWindow = CreateWidget<USWGInventoryWidget>(PlayerController, InventoryClass);
	ShowWindow(InventoryWindow);
}

void USWGUISubsystem::HandleInventoryDockClosed()
{
	InventoryDock = nullptr;
}

void USWGUISubsystem::HandleInputMethodChanged(ECommonInputType InputType)
{
	const bool bGamepad = InputType == ECommonInputType::Gamepad;
	if (TravelWindow)
	{
		TravelWindow->SetControllerMode(bGamepad);
	}
	if (bGamepad)
	{
		ESWGInventoryTab Tab = ESWGInventoryTab::Inventory;
		bool bNeedsDock = false;
		if (MissionWindow) { Tab = ESWGInventoryTab::Missions; bNeedsDock = true; }
		else if (DatapadWindow) { Tab = ESWGInventoryTab::Datapad; bNeedsDock = true; }
		else if (WaypointWindow) { Tab = ESWGInventoryTab::Waypoints; bNeedsDock = true; }
		else if (InventoryWindow) { bNeedsDock = true; }
		if (!bNeedsDock)
		{
			return;
		}

		if (MissionWindow) MissionWindow->Close();
		if (DatapadWindow) DatapadWindow->Close();
		if (WaypointWindow) WaypointWindow->Close();
		if (InventoryWindow) InventoryWindow->Close();
		OpenInventory(true);
		if (InventoryDock)
		{
			if (Tab == ESWGInventoryTab::Missions)
			{
				UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
				USWGMissionSubsystem* Missions = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr;
				InventoryDock->SetMissions(Missions ? Missions->GetMissions() : TArray<FSWGMissionEntry>());
			}
			InventoryDock->SetTab(Tab);
		}
		return;
	}

	if (InventoryDock)
	{
		const ESWGInventoryTab Tab = InventoryDock->GetTab();
		CloseInventory();
		if (Tab == ESWGInventoryTab::Waypoints) ToggleWaypointList();
		else if (Tab == ESWGInventoryTab::Datapad) ToggleDatapad();
		else if (Tab == ESWGInventoryTab::Missions)
		{
			UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
			USWGMissionSubsystem* Missions = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr;
			HandleMissionWindowRequested(Missions ? Missions->GetActiveTerminalId() : 0);
		}
		else OpenInventory(false);
	}
}

void USWGUISubsystem::HandleRadialMenuClosed()
{
	// The dock handed focus to the menu; without this the gamepad would fall through to the world.
	if (InventoryDock)
	{
		InventoryDock->Refocus();
	}
}

void USWGUISubsystem::HandleExamineRequested(int64 ObjectId)
{
	// On a gamepad the dock's details column is the examine view, for world objects too.
	if (IsGamepadActive())
	{
		if (!InventoryDock)
		{
			CloseInventory();
			OpenInventory(true);
		}
		if (InventoryDock)
		{
			InventoryDock->AddExamined(ObjectId);
			InventoryDock->Refocus();
		}
		return;
	}
	OpenExamine(ObjectId);
}

void USWGUISubsystem::OpenExamine(int64 ObjectId)
{
	if (TObjectPtr<USWGExamineWidget>* Existing = ExamineWindows.Find(ObjectId); Existing && *Existing)
	{
		HandleWindowPressed(*Existing);
		return;
	}

	APlayerController* PlayerController = GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
	if (!PlayerController || ObjectId == 0)
	{
		return;
	}
	TSubclassOf<USWGExamineWidget> ExamineClass = USWGUISettings::Get().ExamineClass.LoadSynchronous();
	if (!ExamineClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: no ExamineClass set in Project Settings > SWG UI"));
		return;
	}
	USWGExamineWidget* Window = CreateWidget<USWGExamineWidget>(PlayerController, ExamineClass);
	ExamineWindows.Add(ObjectId, Window);
	ShowWindow(Window);

	// Cascade from the top-left, so a second examine doesn't sit exactly on the first.
	const float Step = 24.f * (ExamineWindows.Num() - 1);
	Window->SetWindowPosition(FVector2D(120.f + Step, 120.f + Step));
	if (!Window->SetObject(ObjectId))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: nothing known about %lld to examine"), ObjectId);
		Window->Close();
	}
}

void USWGUISubsystem::ToggleWaypointList()
{
	if (IsGamepadActive())
	{
		if (InventoryDock && InventoryDock->GetTab() == ESWGInventoryTab::Waypoints)
		{
			InventoryDock->Close();
			return;
		}
		if (!InventoryDock)
		{
			OpenInventory(true);
		}
		if (InventoryDock)
		{
			InventoryDock->SetTab(ESWGInventoryTab::Waypoints);
			InventoryDock->Refocus();
		}
		return;
	}

	if (IsWaypointListOpen())
	{
		CloseWaypointList();
		return;
	}

	APlayerController* PlayerController = GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
	TSubclassOf<USWGWaypointListWidget> WaypointListClass = USWGUISettings::Get().WaypointListClass.LoadSynchronous();
	if (!PlayerController || !WaypointListClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: no WaypointListClass set in Project Settings > SWG UI"));
		return;
	}
	WaypointWindow = CreateWidget<USWGWaypointListWidget>(PlayerController, WaypointListClass);
	ShowWindow(WaypointWindow);
	WaypointWindow->CenterOnScreen();
}

void USWGUISubsystem::CloseWaypointList()
{
	if (WaypointWindow)
	{
		WaypointWindow->Close();
	}
	if (InventoryDock && InventoryDock->GetTab() == ESWGInventoryTab::Waypoints)
	{
		InventoryDock->Close();
	}
}

bool USWGUISubsystem::IsWaypointListOpen() const
{
	return WaypointWindow != nullptr || (InventoryDock && InventoryDock->GetTab() == ESWGInventoryTab::Waypoints);
}

void USWGUISubsystem::ToggleDatapad()
{
	if (IsGamepadActive())
	{
		if (InventoryDock && InventoryDock->GetTab() == ESWGInventoryTab::Datapad)
		{
			InventoryDock->Close();
			return;
		}
		if (!InventoryDock)
		{
			OpenInventory(true);
		}
		if (InventoryDock)
		{
			InventoryDock->SetTab(ESWGInventoryTab::Datapad);
			InventoryDock->Refocus();
		}
		return;
	}

	if (IsDatapadOpen())
	{
		CloseDatapad();
		return;
	}

	APlayerController* PlayerController = GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
	TSubclassOf<USWGDatapadWidget> DatapadClass = USWGUISettings::Get().DatapadClass.LoadSynchronous();
	if (!PlayerController || !DatapadClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: no DatapadClass set in Project Settings > SWG UI"));
		return;
	}
	DatapadWindow = CreateWidget<USWGDatapadWidget>(PlayerController, DatapadClass);
	ShowWindow(DatapadWindow);
	DatapadWindow->CenterOnScreen();
}

void USWGUISubsystem::CloseDatapad()
{
	if (DatapadWindow)
	{
		DatapadWindow->Close();
	}
	if (InventoryDock && InventoryDock->GetTab() == ESWGInventoryTab::Datapad)
	{
		InventoryDock->Close();
	}
}

bool USWGUISubsystem::IsDatapadOpen() const
{
	return DatapadWindow != nullptr || (InventoryDock && InventoryDock->GetTab() == ESWGInventoryTab::Datapad);
}
