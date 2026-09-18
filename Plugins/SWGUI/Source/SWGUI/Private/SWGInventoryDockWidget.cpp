#include "SWGInventoryDockWidget.h"
#include "SWGExamineLines.h"
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
	if (!SWGInventoryQuery::Gather(GetGameInstance(), Equipped, Contents))
	{
		return;
	}
	InventoryTabText->SetText(FText::FromString(FString::Printf(TEXT("INVENTORY (%d)"), Contents.Num())));
	RebuildList();
}

const TArray<FSWGInventoryEntry>& USWGInventoryDockWidget::ActiveEntries() const
{
	switch (Tab)
	{
	case ESWGInventoryTab::Equipped: return Equipped;
	case ESWGInventoryTab::Examine: return Examined;
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
	return Tabs;
}

void USWGInventoryDockWidget::SetTab(ESWGInventoryTab InTab)
{
	Tab = InTab;
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
	EquippedTabText->SetColorAndOpacity(FSlateColor(Tab == ESWGInventoryTab::Equipped ? ActiveTabColor : InactiveTabColor));
	InventoryTabText->SetColorAndOpacity(FSlateColor(Tab == ESWGInventoryTab::Inventory ? ActiveTabColor : InactiveTabColor));
	ExamineTabText->SetColorAndOpacity(FSlateColor(Tab == ESWGInventoryTab::Examine ? ActiveTabColor : InactiveTabColor));
	ExamineTabText->SetVisibility(Examined.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
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

	UGameInstance* GameInstance = GetGameInstance();
	USWGExamineSubsystem* Examine = GameInstance ? GameInstance->GetSubsystem<USWGExamineSubsystem>() : nullptr;
	FSWGExamineInfo Info;
	if (ObjectId == 0 || !Examine || !Examine->Describe(ObjectId, Info))
	{
		DetailNameText->SetText(FText::GetEmpty());
		DetailModel->ClearModel();
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
