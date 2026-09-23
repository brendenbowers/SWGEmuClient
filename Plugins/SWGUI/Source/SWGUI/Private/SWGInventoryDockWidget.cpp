#include "SWGInventoryDockWidget.h"
#include "SWGExamineLines.h"
#include "SWGDatapadQuery.h"
#include "SWGInventoryRowWidget.h"
#include "SWGUISettings.h"
#include "ModelWidget.h"
#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"

namespace
{
	bool IsCursorUp(const FKey& Key) { return Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up || Key == EKeys::Up; }
	bool IsCursorDown(const FKey& Key) { return Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down || Key == EKeys::Down; }

	/** Anything the action bar or the player would otherwise act on: swallowed while the dock is up. */
	bool IsOwnedGamepadKey(const FKey& Key)
	{
		return Key.IsGamepadKey();
	}
}

void USWGInventoryDockWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	DetailModel->SetRotateSpeed(TurntableSpeed);
	SetTab(Tab);
	Refresh();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer, this, &USWGInventoryDockWidget::Refresh, RefreshInterval, true);
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USWGExamineSubsystem* Examine = GameInstance->GetSubsystem<USWGExamineSubsystem>())
		{
			Examine->OnExamineInfo.AddUniqueDynamic(this, &USWGInventoryDockWidget::HandleExamineInfo);
		}
	}
	Refocus();
}

void USWGInventoryDockWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USWGExamineSubsystem* Examine = GameInstance->GetSubsystem<USWGExamineSubsystem>())
		{
			Examine->OnExamineInfo.RemoveDynamic(this, &USWGInventoryDockWidget::HandleExamineInfo);
		}
	}
	Super::NativeDestruct();
}

void USWGInventoryDockWidget::Refresh()
{
	SWGInventoryQuery::Gather(GetGameInstance(), Equipped, Contents);
	RefreshWaypoints();
	RefreshDatapad();
	ApplyTabStyle();
	if (Tab == ESWGInventoryTab::Waypoints || Tab == ESWGInventoryTab::Datapad)
	{
		DetailObjectId = 0;
	}
	RebuildList();
}

const TArray<FSWGInventoryEntry>& USWGInventoryDockWidget::ActiveEntries() const
{
	switch (Tab)
	{
	case ESWGInventoryTab::Equipped: return Equipped;
	case ESWGInventoryTab::Examine: return Examined;
	case ESWGInventoryTab::Waypoints: return WaypointRows;
	case ESWGInventoryTab::Missions: return MissionRows;
	case ESWGInventoryTab::Datapad: return DatapadRows;
	default: return Contents;
	}
}

TArray<ESWGInventoryTab> USWGInventoryDockWidget::AvailableTabs() const
{
	TArray<ESWGInventoryTab> Tabs = { ESWGInventoryTab::Equipped, ESWGInventoryTab::Inventory };
	if (!Examined.IsEmpty())
	{
		Tabs.Add(ESWGInventoryTab::Examine);
	}
	Tabs.Add(ESWGInventoryTab::Waypoints);
	if (!Missions.IsEmpty())
	{
		Tabs.Add(ESWGInventoryTab::Missions);
	}
	Tabs.Add(ESWGInventoryTab::Datapad);
	return Tabs;
}

void USWGInventoryDockWidget::SetTab(ESWGInventoryTab InTab)
{
	Tab = InTab;
	DetailObjectId = 0;
	ApplyTabStyle();
	Cursor = 0;
	RebuildList();
}

void USWGInventoryDockWidget::CycleTab(int32 Direction)
{
	const TArray<ESWGInventoryTab> Tabs = AvailableTabs();
	const int32 Current = FMath::Max(0, Tabs.IndexOfByKey(Tab));
	SetTab(Tabs[(Current + Direction + Tabs.Num()) % Tabs.Num()]);
}

void USWGInventoryDockWidget::ApplyTabStyle()
{
	const bool bInventoryTab = Tab == ESWGInventoryTab::Equipped || Tab == ESWGInventoryTab::Inventory || Tab == ESWGInventoryTab::Examine;
	EquippedTabText->SetVisibility(bInventoryTab ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	ExamineTabText->SetVisibility(bInventoryTab && !Examined.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	InventoryTabText->SetText(bInventoryTab
		? FText::FromString(FString::Printf(TEXT("INVENTORY (%d)"), Contents.Num()))
		: Tab == ESWGInventoryTab::Waypoints
			? FText::FromString(FString::Printf(TEXT("WAYPOINTS (%d)"), WaypointRows.Num()))
			: Tab == ESWGInventoryTab::Missions
				? FText::FromString(FString::Printf(TEXT("MISSIONS (%d)"), MissionRows.Num()))
				: FText::FromString(FString::Printf(TEXT("DATAPAD (%d)"), DatapadRows.Num())));
	EquippedTabText->SetColorAndOpacity(FSlateColor(Tab == ESWGInventoryTab::Equipped ? ActiveTabColor : InactiveTabColor));
	InventoryTabText->SetColorAndOpacity(FSlateColor(Tab == ESWGInventoryTab::Inventory || !bInventoryTab ? ActiveTabColor : InactiveTabColor));
	ExamineTabText->SetColorAndOpacity(FSlateColor(Tab == ESWGInventoryTab::Examine ? ActiveTabColor : InactiveTabColor));
}

void USWGInventoryDockWidget::RefreshWaypoints()
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGWaypointSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<USWGWaypointSubsystem>() : nullptr;
	Waypoints = Subsystem ? Subsystem->GetWaypoints() : TArray<FSWGWaypointEntry>();
	Waypoints.Sort([](const FSWGWaypointEntry& A, const FSWGWaypointEntry& B) { return A.DistanceMeters < B.DistanceMeters; });
	WaypointRows.Reset(Waypoints.Num());
	for (const FSWGWaypointEntry& Waypoint : Waypoints)
	{
		FSWGInventoryEntry& Row = WaypointRows.AddDefaulted_GetRef();
		Row.ObjectId = Waypoint.WaypointObjectId;
		Row.Name = Waypoint.Name.ToString();
		Row.SlotNames = Waypoint.bHasDistance
			? FString::Printf(TEXT("%s  %dm%s"), *Waypoint.Direction, FMath::RoundToInt(Waypoint.DistanceMeters), Waypoint.bActive ? TEXT("  ACTIVE") : TEXT(""))
			: Waypoint.bActive ? TEXT("ACTIVE") : TEXT("");
	}
}

void USWGInventoryDockWidget::RefreshDatapad()
{
	SWGDatapadQuery::Gather(GetGameInstance(), DatapadRows);
}

void USWGInventoryDockWidget::SetMissions(const TArray<FSWGMissionEntry>& InMissions)
{
	Missions = InMissions.FilterByPredicate([](const FSWGMissionEntry& Entry) { return Entry.bPopulated; });
	RebuildMissionRows();
	if (Tab == ESWGInventoryTab::Missions)
	{
		DetailObjectId = 0;
		ApplyTabStyle();
		RebuildList();
	}
}

void USWGInventoryDockWidget::RebuildMissionRows()
{
	MissionRows.Reset(Missions.Num());
	for (const FSWGMissionEntry& Mission : Missions)
	{
		FSWGInventoryEntry& Row = MissionRows.AddDefaulted_GetRef();
		Row.ObjectId = Mission.ObjectId;
		Row.Name = Mission.Title.IsEmpty() ? TEXT("Mission") : Mission.Title.ToString();
		Row.SlotNames = Mission.bHasStartPosition
			? FString::Printf(TEXT("%d cr  %dm %s"), Mission.RewardCredits, FMath::RoundToInt(Mission.DistanceMeters), *Mission.Direction)
			: FString::Printf(TEXT("%d cr"), Mission.RewardCredits);
	}
}

void USWGInventoryDockWidget::MoveCursor(int32 Delta)
{
	if (Rows.IsEmpty())
	{
		return;
	}
	Cursor = FMath::Clamp(Cursor + Delta, 0, Rows.Num() - 1);
	ApplyCursor();
}

void USWGInventoryDockWidget::AddExamined(int64 ObjectId)
{
	FSWGInventoryEntry Entry = SWGInventoryQuery::Describe(GetGameInstance(), ObjectId);
	// The examine subsystem knows names for things that aren't tangibles (creatures, players).
	UGameInstance* GameInstance = GetGameInstance();
	USWGExamineSubsystem* Examine = GameInstance ? GameInstance->GetSubsystem<USWGExamineSubsystem>() : nullptr;
	FSWGExamineInfo Info;
	if (Examine && Examine->Describe(ObjectId, Info) && !Info.Name.IsEmpty())
	{
		Entry.Name = Info.Name;
	}

	Examined.RemoveAll([ObjectId](const FSWGInventoryEntry& Existing) { return Existing.ObjectId == ObjectId; });
	Examined.Insert(MoveTemp(Entry), 0);
	if (Examined.Num() > ExamineHistory)
	{
		Examined.SetNum(ExamineHistory);
	}
	SetTab(ESWGInventoryTab::Examine);
}

void USWGInventoryDockWidget::RebuildList()
{
	// Keep the cursor on the same item across a refresh when it is still there.
	const int64 CursorObjectId = Rows.IsValidIndex(Cursor) ? Rows[Cursor]->GetObjectId() : 0;

	ListPanel->ClearChildren();
	Rows.Reset();

	const TArray<FSWGInventoryEntry>& Entries = ActiveEntries();
	ListEmptyText->SetVisibility(Entries.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	TSubclassOf<USWGInventoryRowWidget> RowClass = USWGUISettings::Get().InventoryRowClass.LoadSynchronous();
	if (!RowClass)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGInventoryDockWidget: SWG UI settings have no InventoryRowClass"));
		return;
	}

	for (const FSWGInventoryEntry& Entry : Entries)
	{
		USWGInventoryRowWidget* Row = CreateWidget<USWGInventoryRowWidget>(this, RowClass);
		Row->SetRow(Entry.ObjectId, Entry.Label(), Entry.SlotNames);
		Row->OnPressed.BindUObject(this, &USWGInventoryDockWidget::HandleRowPressed);
		ListPanel->AddChild(Row);
		Rows.Add(Row);
	}

	const int32 Found = Rows.IndexOfByPredicate([CursorObjectId](const USWGInventoryRowWidget* Row) { return Row->GetObjectId() == CursorObjectId; });
	Cursor = Found != INDEX_NONE ? Found : FMath::Clamp(Cursor, 0, FMath::Max(0, Rows.Num() - 1));
	ApplyCursor();
}
void USWGInventoryDockWidget::ApplyCursor()
{
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Rows[Index]->SetSelected(Index == Cursor);
	}

	if (!Rows.IsValidIndex(Cursor))
	{
		ShowDetails(0);
		return;
	}
	if (UScrollBox* Scroll = Cast<UScrollBox>(ListPanel))
	{
		Scroll->ScrollWidgetIntoView(Rows[Cursor], true, EDescendantScrollDestination::IntoView);
	}
	ShowDetails(Rows[Cursor]->GetObjectId());
}

void USWGInventoryDockWidget::ShowDetails(int64 ObjectId)
{
	if (ObjectId == DetailObjectId)
	{
		return;
	}
	DetailObjectId = ObjectId;
	DetailAttributePanel->ClearChildren();
	DetailDescriptionText->SetText(FText::GetEmpty());
	DetailDescriptionText->SetVisibility(ESlateVisibility::Collapsed);
	DetailModel->ClearModel();

	if (Tab == ESWGInventoryTab::Waypoints)
	{
		const FSWGWaypointEntry* Waypoint = Waypoints.FindByPredicate([ObjectId](const FSWGWaypointEntry& Entry) { return Entry.WaypointObjectId == ObjectId; });
		DetailNameText->SetText(Waypoint ? Waypoint->Name : FText::GetEmpty());
		if (Waypoint)
		{
			FText Details = FText::FromString(Waypoint->bHasDistance
				? FString::Printf(TEXT("%s, %d metres away%s"), *Waypoint->Direction, FMath::RoundToInt(Waypoint->DistanceMeters), Waypoint->bActive ? TEXT(" (active)") : TEXT(""))
				: Waypoint->bActive ? TEXT("Active waypoint") : TEXT("Inactive waypoint"));

			UGameInstance* GameInstance = GetGameInstance();
			USWGMissionSubsystem* MissionSubsystem = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr;
			const TArray<FSWGMissionEntry> MissionEntries = MissionSubsystem ? MissionSubsystem->GetAllTrackedMissions() : TArray<FSWGMissionEntry>();
			if (const FSWGMissionEntry* Mission = MissionEntries.FindByPredicate([ObjectId](const FSWGMissionEntry& Entry) { return Entry.WaypointObjectId == ObjectId; }))
			{
				DetailNameText->SetText(Mission->Title.IsEmpty() ? Waypoint->Name : Mission->Title);
				Details = FText::Format(NSLOCTEXT("SWGEmu", "WaypointMissionDetails", "{0}\n\n{1}\nReward: {2} cr   Difficulty: {3}{4}"),
					Details, Mission->Description, FText::AsNumber(Mission->RewardCredits), FText::AsNumber(Mission->DifficultyDisplay),
					Mission->TargetTemplateName.IsEmpty() ? FText::GetEmpty() : FText::Format(NSLOCTEXT("SWGEmu", "WaypointMissionTarget", "   Target: {0}"), Mission->TargetTemplateName));
			}

			DetailDescriptionText->SetText(Details);
			DetailDescriptionText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		return;
	}

	if (Tab == ESWGInventoryTab::Missions)
	{
		const FSWGMissionEntry* Mission = Missions.FindByPredicate([ObjectId](const FSWGMissionEntry& Entry) { return Entry.ObjectId == ObjectId; });
		DetailNameText->SetText(Mission ? Mission->Title : FText::GetEmpty());
		if (Mission)
		{
			DetailDescriptionText->SetText(FText::Format(NSLOCTEXT("SWGEmu", "ControllerMissionDetails", "{0}\nReward: {1} cr   Difficulty: {2}{3}"),
				Mission->Description, FText::AsNumber(Mission->RewardCredits), FText::AsNumber(Mission->DifficultyDisplay),
				Mission->TargetTemplateName.IsEmpty() ? FText::GetEmpty() : FText::Format(NSLOCTEXT("SWGEmu", "ControllerMissionTarget", "   Target: {0}"), Mission->TargetTemplateName)));
			DetailDescriptionText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	USWGExamineSubsystem* Examine = GameInstance ? GameInstance->GetSubsystem<USWGExamineSubsystem>() : nullptr;
	FSWGExamineInfo Info;
	if (ObjectId == 0 || !Examine || !Examine->Describe(ObjectId, Info))
	{
		const FSWGInventoryEntry* Entry = ActiveEntries().FindByPredicate([ObjectId](const FSWGInventoryEntry& Item) { return Item.ObjectId == ObjectId; });
		DetailNameText->SetText(Entry ? FText::FromString(Entry->Name) : FText::GetEmpty());
		if (Tab == ESWGInventoryTab::Datapad)
		{
			USWGMissionSubsystem* MissionSubsystem = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr;
			if (const FSWGMissionEntry* Mission = MissionSubsystem ? MissionSubsystem->FindMission(ObjectId) : nullptr)
			{
				DetailDescriptionText->SetText(Mission->Description);
				DetailDescriptionText->SetVisibility(Mission->Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
			}
		}
		return;
	}
	DetailModel->SetObject(ObjectId);
	HandleExamineInfo(Info);
}

void USWGInventoryDockWidget::HandleExamineInfo(const FSWGExamineInfo& Info)
{
	if (Info.ObjectId != DetailObjectId)
	{
		return;
	}
	DetailNameText->SetText(FText::FromString(Info.Name));
	DetailDescriptionText->SetText(FText::FromString(Info.Description));
	DetailDescriptionText->SetVisibility(Info.Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);

	SWGExamineLines::Fill(this, DetailAttributePanel, Info);
}

void USWGInventoryDockWidget::OpenActions()
{
	if (Tab == ESWGInventoryTab::Missions)
	{
		UGameInstance* GameInstance = GetGameInstance();
		if (USWGMissionSubsystem* MissionSubsystem = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr; MissionSubsystem && DetailObjectId != 0)
		{
			MissionSubsystem->AcceptMission(DetailObjectId);
		}
		return;
	}
	if (Tab == ESWGInventoryTab::Waypoints)
	{
		UGameInstance* GameInstance = GetGameInstance();
		if (USWGWaypointSubsystem* WaypointSubsystem = GameInstance ? GameInstance->GetSubsystem<USWGWaypointSubsystem>() : nullptr)
		{
			WaypointSubsystem->ToggleWaypoint(DetailObjectId);
		}
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	USWGRadialMenuSubsystem* Radial = GameInstance ? GameInstance->GetSubsystem<USWGRadialMenuSubsystem>() : nullptr;
	if (!Radial || DetailObjectId == 0)
	{
		return;
	}
	// Beside the focused row, like a right-click would.
	const FGeometry& AnchorGeometry = Rows.IsValidIndex(Cursor) ? Rows[Cursor]->GetCachedGeometry() : DetailModel->GetCachedGeometry();
	FVector2D Pixel, Viewport;
	USlateBlueprintLibrary::AbsoluteToViewport(this, AnchorGeometry.GetAbsolutePosition() + FVector2D(0.f, AnchorGeometry.GetAbsoluteSize().Y), Pixel, Viewport);
	Radial->RequestMenu(DetailObjectId, Pixel);
}

void USWGInventoryDockWidget::HandleRowPressed(int64 ObjectId, FKey Button, FVector2D ScreenPosition)
{
	// A mouse can still click rows in the dock; treat it as moving the cursor there.
	const int32 Index = Rows.IndexOfByPredicate([ObjectId](const USWGInventoryRowWidget* Row) { return Row->GetObjectId() == ObjectId; });
	if (Index != INDEX_NONE)
	{
		Cursor = Index;
		ApplyCursor();
		if (Button == EKeys::RightMouseButton)
		{
			OpenActions();
		}
	}
}

void USWGInventoryDockWidget::Close()
{
	RemoveFromParent();
	OnClosed.Broadcast();
}

void USWGInventoryDockWidget::Refocus()
{
	SetFocus();
}

FReply USWGInventoryDockWidget::NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent)
{
	return FReply::Handled();
}

FReply USWGInventoryDockWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (IsCursorUp(Key))
	{
		MoveCursor(-1);
		return FReply::Handled();
	}
	if (IsCursorDown(Key))
	{
		MoveCursor(1);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_LeftShoulder)
	{
		CycleTab(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_RightShoulder || Key == EKeys::Tab)
	{
		CycleTab(1);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Top && Tab == ESWGInventoryTab::Missions)
	{
		UGameInstance* GameInstance = GetGameInstance();
		if (USWGMissionSubsystem* MissionSubsystem = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr)
		{
			MissionSubsystem->RefreshMissionList();
		}
		return FReply::Handled();
	}
	// The same button that opens a target's menu out in the world (ASWGPlayer::InteractKey), plus A.
	if (Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Enter)
	{
		OpenActions();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Left || Key == EKeys::Escape)
	{
		Close();
		return FReply::Handled();
	}
	if (IsOwnedGamepadKey(Key))
	{
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply USWGInventoryDockWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Releases must not reach the player either, or a slot could fire on the way out.
	return IsOwnedGamepadKey(InKeyEvent.GetKey()) ? FReply::Handled() : Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}
