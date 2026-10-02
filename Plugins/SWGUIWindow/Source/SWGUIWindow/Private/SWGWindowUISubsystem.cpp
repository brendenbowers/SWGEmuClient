#include "SWGWindowUISubsystem.h"
#include "SWGWindowUISettings.h"
#include "SWGUISubsystem.h"
#include "SWGWindowWidget.h"
#include "SWGMissionBrowserWidget.h"
#include "SWGMissionBrowserDockWidget.h"
#include "SWGTravelWidget.h"
#include "SWGInventoryWidget.h"
#include "SWGInventoryDockWidget.h"
#include "SWGExamineWidget.h"
#include "SWGWaypointListWidget.h"
#include "SWGDatapadWidget.h"
#include "SWGPlanetMapWindowWidget.h"
#include "SWGSurveyWidget.h"
#include "SWGCraftingWidget.h"
#include "SWGStructurePlacementWidget.h"
#include "CommonInputSubsystem.h"
#include "Subsystems/SWGMissionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

void USWGWindowUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The router first, so it exists to register with.
	if (USWGUISubsystem* Router = Collection.InitializeDependency<USWGUISubsystem>())
	{
		Router->RegisterPresenter(*this);
	}
	if (UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(GetLocalPlayer()))
	{
		InputMethodChangedHandle = CommonInput->OnInputMethodChangedNative.AddUObject(this, &USWGWindowUISubsystem::HandleInputMethodChanged);
	}
}

void USWGWindowUISubsystem::Deinitialize()
{
	if (UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(GetLocalPlayer()))
	{
		CommonInput->OnInputMethodChangedNative.Remove(InputMethodChangedHandle);
	}
	if (USWGUISubsystem* Router = GetRouter())
	{
		Router->UnregisterPresenter(*this);
	}
	Super::Deinitialize();
}

USWGUISubsystem* USWGWindowUISubsystem::GetRouter() const
{
	return GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<USWGUISubsystem>() : nullptr;
}

APlayerController* USWGWindowUISubsystem::GetController() const
{
	return GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
}

bool USWGWindowUISubsystem::IsGamepadActive() const
{
	const USWGUISubsystem* Router = GetRouter();
	return Router && Router->IsGamepadActive();
}

// ── ISWGUIPresenter ──────────────────────────────────────────────────────────

bool USWGWindowUISubsystem::SupportsFeature(ESWGUIFeature Feature) const
{
	return true;
}

void USWGWindowUISubsystem::OpenFeature(ESWGUIFeature Feature, const FSWGUIFeatureRequest& Request)
{
	switch (Feature)
	{
	case ESWGUIFeature::Inventory:
		if (!IsInventoryOpen()) { OpenInventory(Request.bDocked || IsGamepadActive()); }
		break;
	case ESWGUIFeature::PlanetMap:
		if (PlanetMapWindow) { HandleWindowPressed(PlanetMapWindow); } else { OpenPlanetMap(); }
		break;
	case ESWGUIFeature::Survey:
		// Using the tool again with the survey up just refreshes it in place.
		if (SurveyWindow) { HandleWindowPressed(SurveyWindow); } else { OpenSurvey(); }
		break;
	case ESWGUIFeature::Crafting:
		// A new "Use" on the tool with the window already up just refreshes it in place
		// (USWGCraftingWidget::HandleSessionStarted rebuilds the schematic list itself).
		if (CraftingWindow) { HandleWindowPressed(CraftingWindow); } else { OpenCrafting(); }
		break;
	case ESWGUIFeature::Examine:
		OpenExamineFromRequest(Request.ObjectId);
		break;
	case ESWGUIFeature::WaypointList:
		if (!IsWaypointListOpen()) { ToggleWaypointList(); }
		break;
	case ESWGUIFeature::Datapad:
		if (!IsDatapadOpen()) { ToggleDatapad(); }
		break;
	case ESWGUIFeature::MissionBrowser:
		OpenMissionBrowser(Request.ObjectId);
		break;
	case ESWGUIFeature::Travel:
		OpenTravel();
		break;
	case ESWGUIFeature::StructurePlacement:
		OpenPlacement();
		break;
	}
}

void USWGWindowUISubsystem::CloseFeature(ESWGUIFeature Feature)
{
	switch (Feature)
	{
	case ESWGUIFeature::Inventory: CloseInventory(); break;
	case ESWGUIFeature::PlanetMap: if (PlanetMapWindow) { PlanetMapWindow->Close(); } break;
	case ESWGUIFeature::Survey: if (SurveyWindow) { SurveyWindow->Close(); } break;
	case ESWGUIFeature::Crafting: if (CraftingWindow) { CraftingWindow->Close(); } break;
	case ESWGUIFeature::Examine:
	{
		TArray<TObjectPtr<USWGExamineWidget>> Open;
		ExamineWindows.GenerateValueArray(Open);
		for (USWGExamineWidget* Window : Open) { if (Window) { Window->Close(); } }
		break;
	}
	case ESWGUIFeature::WaypointList: CloseWaypointList(); break;
	case ESWGUIFeature::Datapad: CloseDatapad(); break;
	case ESWGUIFeature::MissionBrowser:
		if (MissionWindow) { MissionWindow->Close(); }
		if (MissionDock) { MissionDock->Close(); }
		break;
	case ESWGUIFeature::Travel: if (TravelWindow) { TravelWindow->Close(); } break;
	case ESWGUIFeature::StructurePlacement: if (PlacementWidget) { PlacementWidget->Close(); } break;
	}
}

bool USWGWindowUISubsystem::IsFeatureOpen(ESWGUIFeature Feature) const
{
	switch (Feature)
	{
	case ESWGUIFeature::Inventory: return IsInventoryOpen();
	case ESWGUIFeature::PlanetMap: return PlanetMapWindow != nullptr;
	case ESWGUIFeature::Survey: return SurveyWindow != nullptr;
	case ESWGUIFeature::Crafting: return CraftingWindow != nullptr;
	case ESWGUIFeature::Examine: return !ExamineWindows.IsEmpty();
	case ESWGUIFeature::WaypointList: return IsWaypointListOpen();
	case ESWGUIFeature::Datapad: return IsDatapadOpen();
	case ESWGUIFeature::MissionBrowser: return MissionWindow != nullptr || MissionDock != nullptr;
	case ESWGUIFeature::Travel: return TravelWindow != nullptr;
	case ESWGUIFeature::StructurePlacement: return PlacementWidget != nullptr && PlacementWidget->IsInViewport();
	}
	return false;
}

void USWGWindowUISubsystem::ToggleFeature(ESWGUIFeature Feature)
{
	switch (Feature)
	{
	case ESWGUIFeature::Inventory: ToggleInventory(); break;
	case ESWGUIFeature::WaypointList: ToggleWaypointList(); break;
	case ESWGUIFeature::Datapad: ToggleDatapad(); break;
	default: ISWGUIPresenter::ToggleFeature(Feature); break;
	}
}

void USWGWindowUISubsystem::NotifyDataChanged(ESWGUIFeature Feature)
{
	if (Feature == ESWGUIFeature::MissionBrowser)
	{
		RefreshMissionLists();
	}
}

void USWGWindowUISubsystem::NotifyRadialMenuClosed()
{
	// The dock handed focus to the menu; without this the gamepad would fall through to the world.
	if (InventoryDock)
	{
		InventoryDock->Refocus();
	}
}

// ── Floating windows ─────────────────────────────────────────────────────────

void USWGWindowUISubsystem::ShowWindow(USWGWindowWidget* Window)
{
	if (!Window)
	{
		return;
	}
	Windows.AddUnique(Window);
	Window->OnPressed.AddUObject(this, &USWGWindowUISubsystem::HandleWindowPressed);
	Window->OnClosed.AddUObject(this, &USWGWindowUISubsystem::HandleWindowClosed);
	Window->AddToPlayerScreen(NextWindowZ++);
	Window->SetFocus();
}

void USWGWindowUISubsystem::HandleWindowPressed(USWGWindowWidget* Window)
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

void USWGWindowUISubsystem::HandleWindowClosed(USWGWindowWidget* Window)
{
	Windows.Remove(Window);
	if (Window == InventoryWindow) { InventoryWindow = nullptr; }
	if (Window == MissionWindow) { MissionWindow = nullptr; }
	if (Window == TravelWindow) { TravelWindow = nullptr; }
	if (Window == WaypointWindow) { WaypointWindow = nullptr; }
	if (Window == DatapadWindow) { DatapadWindow = nullptr; }
	if (Window == PlanetMapWindow) { PlanetMapWindow = nullptr; }
	if (Window == SurveyWindow)
	{
		SurveyWindow = nullptr;
		if (USWGUISubsystem* Router = GetRouter()) { Router->NotifyFeatureStateChanged(ESWGUIFeature::Survey); }
	}
	if (Window == CraftingWindow) { CraftingWindow = nullptr; }
	for (auto It = ExamineWindows.CreateIterator(); It; ++It)
	{
		if (It->Value == Window)
		{
			It.RemoveCurrent();
		}
	}
}

// ── Inventory ────────────────────────────────────────────────────────────────

bool USWGWindowUISubsystem::IsInventoryOpen() const
{
	return InventoryWindow != nullptr || InventoryDock != nullptr;
}

void USWGWindowUISubsystem::ToggleInventory()
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

void USWGWindowUISubsystem::CloseInventory()
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

void USWGWindowUISubsystem::OpenInventory(bool bDocked)
{
	APlayerController* PlayerController = GetController();
	if (!PlayerController)
	{
		return;
	}

	if (bDocked)
	{
		TSubclassOf<USWGInventoryDockWidget> DockClass = USWGWindowUISettings::Get().InventoryDockClass.LoadSynchronous();
		if (!DockClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGWindowUISubsystem: no InventoryDockClass set in Project Settings > SWG UI (Window)"));
			return;
		}
		InventoryDock = CreateWidget<USWGInventoryDockWidget>(PlayerController, DockClass);
		InventoryDock->OnClosed.AddUObject(this, &USWGWindowUISubsystem::HandleInventoryDockClosed);
		// Same band as the floating windows: over the HUD, under the radial menu.
		InventoryDock->AddToPlayerScreen(NextWindowZ++);
		return;
	}

	TSubclassOf<USWGInventoryWidget> InventoryClass = USWGWindowUISettings::Get().InventoryClass.LoadSynchronous();
	if (!InventoryClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGWindowUISubsystem: no InventoryClass set in Project Settings > SWG UI (Window)"));
		return;
	}
	InventoryWindow = CreateWidget<USWGInventoryWidget>(PlayerController, InventoryClass);
	ShowWindow(InventoryWindow);
}

void USWGWindowUISubsystem::HandleInventoryDockClosed()
{
	InventoryDock = nullptr;
}

void USWGWindowUISubsystem::HandleInputMethodChanged(ECommonInputType InputType)
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
			OpenMissionBrowser(Missions ? Missions->GetActiveTerminalId() : 0);
		}
		else OpenInventory(false);
	}
}

// ── Examine ──────────────────────────────────────────────────────────────────

void USWGWindowUISubsystem::OpenExamineFromRequest(int64 ObjectId)
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

void USWGWindowUISubsystem::OpenExamine(int64 ObjectId)
{
	if (TObjectPtr<USWGExamineWidget>* Existing = ExamineWindows.Find(ObjectId); Existing && *Existing)
	{
		HandleWindowPressed(*Existing);
		return;
	}

	APlayerController* PlayerController = GetController();
	if (!PlayerController || ObjectId == 0)
	{
		return;
	}
	TSubclassOf<USWGExamineWidget> ExamineClass = USWGWindowUISettings::Get().ExamineClass.LoadSynchronous();
	if (!ExamineClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGWindowUISubsystem: no ExamineClass set in Project Settings > SWG UI (Window)"));
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
		UE_LOG(LogTemp, Warning, TEXT("USWGWindowUISubsystem: nothing known about %lld to examine"), ObjectId);
		Window->Close();
	}
}

// ── Waypoint list and datapad ────────────────────────────────────────────────

void USWGWindowUISubsystem::ToggleWaypointList()
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

	APlayerController* PlayerController = GetController();
	TSubclassOf<USWGWaypointListWidget> WaypointListClass = USWGWindowUISettings::Get().WaypointListClass.LoadSynchronous();
	if (!PlayerController || !WaypointListClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGWindowUISubsystem: no WaypointListClass set in Project Settings > SWG UI (Window)"));
		return;
	}
	WaypointWindow = CreateWidget<USWGWaypointListWidget>(PlayerController, WaypointListClass);
	ShowWindow(WaypointWindow);
	WaypointWindow->CenterOnScreen();
}

void USWGWindowUISubsystem::CloseWaypointList()
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

bool USWGWindowUISubsystem::IsWaypointListOpen() const
{
	return WaypointWindow != nullptr || (InventoryDock && InventoryDock->GetTab() == ESWGInventoryTab::Waypoints);
}

void USWGWindowUISubsystem::ToggleDatapad()
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

	APlayerController* PlayerController = GetController();
	TSubclassOf<USWGDatapadWidget> DatapadClass = USWGWindowUISettings::Get().DatapadClass.LoadSynchronous();
	if (!PlayerController || !DatapadClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGWindowUISubsystem: no DatapadClass set in Project Settings > SWG UI (Window)"));
		return;
	}
	DatapadWindow = CreateWidget<USWGDatapadWidget>(PlayerController, DatapadClass);
	ShowWindow(DatapadWindow);
	DatapadWindow->CenterOnScreen();
}

void USWGWindowUISubsystem::CloseDatapad()
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

bool USWGWindowUISubsystem::IsDatapadOpen() const
{
	return DatapadWindow != nullptr || (InventoryDock && InventoryDock->GetTab() == ESWGInventoryTab::Datapad);
}

// ── Planet map, survey, crafting ─────────────────────────────────────────────

void USWGWindowUISubsystem::OpenPlanetMap()
{
	APlayerController* PlayerController = GetController();
	TSubclassOf<USWGPlanetMapWindowWidget> PlanetMapClass = USWGWindowUISettings::Get().PlanetMapClass.LoadSynchronous();
	if (!PlayerController || !PlanetMapClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGWindowUISubsystem: no PlanetMapClass set in Project Settings > SWG UI (Window)"));
		return;
	}
	PlanetMapWindow = CreateWidget<USWGPlanetMapWindowWidget>(PlayerController, PlanetMapClass);
	PlanetMapWindow->SetControllerMode(IsGamepadActive());
	ShowWindow(PlanetMapWindow);
	PlanetMapWindow->CenterOnScreen();
}

void USWGWindowUISubsystem::OpenSurvey()
{
	APlayerController* PlayerController = GetController();
	if (!PlayerController)
	{
		return;
	}
	TSubclassOf<USWGSurveyWidget> SurveyClass = USWGWindowUISettings::Get().SurveyClass.LoadSynchronous();
	if (!SurveyClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGWindowUISubsystem: no SurveyClass set in Project Settings > SWG UI (Window)"));
		return;
	}
	SurveyWindow = CreateWidget<USWGSurveyWidget>(PlayerController, SurveyClass);
	ShowWindow(SurveyWindow);
	SurveyWindow->CenterOnScreen();
}

void USWGWindowUISubsystem::OpenCrafting()
{
	APlayerController* PlayerController = GetController();
	if (!PlayerController) { return; }
	TSubclassOf<USWGCraftingWidget> CraftingClass = USWGWindowUISettings::Get().CraftingClass.LoadSynchronous();
	if (!CraftingClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGWindowUISubsystem: no CraftingClass set in Project Settings > SWG UI (Window)"));
		return;
	}
	CraftingWindow = CreateWidget<USWGCraftingWidget>(PlayerController, CraftingClass);
	ShowWindow(CraftingWindow);
	CraftingWindow->CenterOnScreen();
}

// ── Missions and travel ──────────────────────────────────────────────────────

void USWGWindowUISubsystem::OpenMissionBrowser(int64 TerminalObjectId)
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
		// Already up (re-used the terminal, or a second terminal) — raise it and refresh it.
		if (MissionWindow)
		{
			HandleWindowPressed(MissionWindow);
		}
		RefreshMissionLists();
		return;
	}

	APlayerController* PlayerController = GetController();
	if (!PlayerController)
	{
		return;
	}

	TSubclassOf<USWGMissionBrowserWidget> BrowserClass = USWGWindowUISettings::Get().MissionBrowserClass.LoadSynchronous();
	if (!BrowserClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGWindowUISubsystem: mission window for %lld dropped — no MissionBrowserClass set in Project Settings > SWG UI (Window)"), TerminalObjectId);
		return;
	}

	MissionWindow = CreateWidget<USWGMissionBrowserWidget>(PlayerController, BrowserClass);
	ShowWindow(MissionWindow);
	MissionWindow->CenterOnScreen();
	RefreshMissionLists();
}

void USWGWindowUISubsystem::HandleMissionDockClosed()
{
	MissionDock = nullptr;
}

void USWGWindowUISubsystem::RefreshMissionLists()
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

void USWGWindowUISubsystem::OpenTravel()
{
	if (TravelWindow)
	{
		TravelWindow->SetControllerMode(IsGamepadActive());
		HandleWindowPressed(TravelWindow);
		return;
	}

	APlayerController* PlayerController = GetController();
	if (!PlayerController)
	{
		return;
	}
	TSubclassOf<USWGTravelWidget> TravelClass = USWGWindowUISettings::Get().TravelClass.LoadSynchronous();
	if (!TravelClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGWindowUISubsystem: no TravelClass set in Project Settings > SWG UI (Window)"));
		return;
	}
	TravelWindow = CreateWidget<USWGTravelWidget>(PlayerController, TravelClass);
	TravelWindow->SetControllerMode(IsGamepadActive());
	ShowWindow(TravelWindow);
	TravelWindow->CenterOnScreen();
}

// ── Structure placement ──────────────────────────────────────────────────────

void USWGWindowUISubsystem::OpenPlacement()
{
	APlayerController* PlayerController = GetController();
	if (!PlayerController) { return; }
	if (PlacementWidget) { PlacementWidget->Close(); }
	PlacementWidget = CreateWidget<USWGStructurePlacementWidget>(PlayerController);
	if (PlacementWidget) { PlacementWidget->AddToPlayerScreen(250); }
}
