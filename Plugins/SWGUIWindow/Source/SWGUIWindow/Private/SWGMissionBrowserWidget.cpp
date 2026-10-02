#include "SWGMissionBrowserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "ModelWidget.h"
#include "Subsystems/SWGItemIconSubsystem.h"
#include "Engine/GameInstance.h"

void USWGMissionBrowserWidget::SetMissions(const TArray<FSWGMissionEntry>& InMissions)
{
	Missions = InMissions;
	if (IsConstructed())
	{
		BuildList();
	}
}

void USWGMissionBrowserWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetTitle(NSLOCTEXT("SWGEmu", "MissionTitle", "Mission Browser"));

	if (NameHeaderButton)       { NameHeaderButton->OnClicked.AddUniqueDynamic(this, &USWGMissionBrowserWidget::HandleSortByName); }
	if (CostHeaderButton)       { CostHeaderButton->OnClicked.AddUniqueDynamic(this, &USWGMissionBrowserWidget::HandleSortByCost); }
	if (DifficultyHeaderButton) { DifficultyHeaderButton->OnClicked.AddUniqueDynamic(this, &USWGMissionBrowserWidget::HandleSortByDifficulty); }
	if (DistanceHeaderButton)   { DistanceHeaderButton->OnClicked.AddUniqueDynamic(this, &USWGMissionBrowserWidget::HandleSortByDistance); }
	if (DirectionHeaderButton)  { DirectionHeaderButton->OnClicked.AddUniqueDynamic(this, &USWGMissionBrowserWidget::HandleSortByDirection); }
	if (AcceptButton)         { AcceptButton->OnClicked.AddUniqueDynamic(this, &USWGMissionBrowserWidget::HandleAcceptClicked); }
	if (RefreshButton)        { RefreshButton->OnClicked.AddUniqueDynamic(this, &USWGMissionBrowserWidget::HandleRefreshClicked); }

	ApplyDetailsRowHeight();
	BuildList();
	ApplyDetails();
}

UWidget* USWGMissionBrowserWidget::FindSplitterUnderMouse(const FVector2D& ScreenPosition) const
{
	for (UWidget* Splitter : { CostSplitter.Get(), DifficultySplitter.Get(), DistanceSplitter.Get(), DirectionSplitter.Get() })
	{
		if (!Splitter)
		{
			continue;
		}
		const FGeometry& Geometry = Splitter->GetCachedGeometry();
		const FVector2D Local = Geometry.AbsoluteToLocal(ScreenPosition);
		const FVector2D Size = Geometry.GetLocalSize();
		// The handle is a few px wide; accept a bit either side so it's easy to grab.
		if (Local.X >= -4.f && Local.X <= Size.X + 4.f && Local.Y >= 0.f && Local.Y <= Size.Y)
		{
			return Splitter;
		}
	}
	return nullptr;
}

float* USWGMissionBrowserWidget::GetColumnWidthFor(UWidget* Splitter)
{
	if (Splitter == CostSplitter)       { return &CostColumnWidth; }
	if (Splitter == DifficultySplitter) { return &DifficultyColumnWidth; }
	if (Splitter == DistanceSplitter)   { return &DistanceColumnWidth; }
	if (Splitter == DirectionSplitter)  { return &DirectionColumnWidth; }
	return nullptr;
}

USizeBox* USWGMissionBrowserWidget::GetHeaderBoxFor(UWidget* Splitter) const
{
	if (Splitter == CostSplitter)       { return CostHeaderBox; }
	if (Splitter == DifficultySplitter) { return DifficultyHeaderBox; }
	if (Splitter == DistanceSplitter)   { return DistanceHeaderBox; }
	if (Splitter == DirectionSplitter)  { return DirectionHeaderBox; }
	return nullptr;
}

bool USWGMissionBrowserWidget::IsOverDetailsRowSplitter(const FVector2D& ScreenPosition) const
{
	if (!DetailsRowSplitter)
	{
		return false;
	}
	const FGeometry& Geometry = DetailsRowSplitter->GetCachedGeometry();
	const FVector2D Local = Geometry.AbsoluteToLocal(ScreenPosition);
	const FVector2D Size = Geometry.GetLocalSize();
	// A few px above/below the handle so it's easy to grab, matching FindSplitterUnderMouse.
	return Local.X >= 0.f && Local.X <= Size.X && Local.Y >= -4.f && Local.Y <= Size.Y + 4.f;
}

void USWGMissionBrowserWidget::ApplyDetailsRowHeight()
{
	if (DetailsDescriptionBox)
	{
		DetailsDescriptionBox->SetHeightOverride(DetailsRowHeight);
	}
	if (DetailModelBox)
	{
		DetailModelBox->SetHeightOverride(DetailsRowHeight);
	}
}

FCursorReply USWGMissionBrowserWidget::NativeOnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InCursorEvent)
{
	if (bDraggingDetailsRow || IsOverDetailsRowSplitter(InCursorEvent.GetScreenSpacePosition()))
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
	}
	if (DraggingSplitter || FindSplitterUnderMouse(InCursorEvent.GetScreenSpacePosition()))
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}
	return Super::NativeOnCursorQuery(InGeometry, InCursorEvent);
}

FReply USWGMissionBrowserWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (IsOverDetailsRowSplitter(InMouseEvent.GetScreenSpacePosition()))
		{
			OnPressed.Broadcast(this);
			bDraggingDetailsRow = true;
			DetailsRowDragStartHeight = DetailsRowHeight;
			DetailsRowDragStartMouse = InMouseEvent.GetScreenSpacePosition();
			return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget(), EFocusCause::Mouse);
		}
		if (UWidget* Splitter = FindSplitterUnderMouse(InMouseEvent.GetScreenSpacePosition()))
		{
			if (const float* Width = GetColumnWidthFor(Splitter))
			{
				OnPressed.Broadcast(this);
				DraggingSplitter = Splitter;
				DragStartWidth = *Width;
				DragStartMouse = InMouseEvent.GetScreenSpacePosition();
				return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget(), EFocusCause::Mouse);
			}
		}
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply USWGMissionBrowserWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingDetailsRow)
	{
		// The splitter now sits ABOVE the details pane (between it and the mission
		// list), so dragging it down should grow the list and shrink the details
		// pane below — the opposite sign from a splitter sitting inside the pane.
		const float Scale = FMath::Max(InGeometry.Scale, KINDA_SMALL_NUMBER);
		const float Delta = (InMouseEvent.GetScreenSpacePosition().Y - DetailsRowDragStartMouse.Y) / Scale;
		DetailsRowHeight = FMath::Clamp(DetailsRowDragStartHeight - Delta, MinimumDetailsRowHeight, MaximumDetailsRowHeight);
		ApplyDetailsRowHeight();
		return FReply::Handled();
	}
	if (DraggingSplitter)
	{
		float* Width = GetColumnWidthFor(DraggingSplitter);
		if (!Width)
		{
			DraggingSplitter = nullptr;
			return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
		}

		// Screen pixels to slate units, so the column tracks the cursor at any DPI.
		// The splitter sits at the column's LEFT edge, so dragging it right shrinks
		// the column (its content is right-aligned against a fixed right edge).
		const float Scale = FMath::Max(InGeometry.Scale, KINDA_SMALL_NUMBER);
		const float Delta = (InMouseEvent.GetScreenSpacePosition().X - DragStartMouse.X) / Scale;
		*Width = FMath::Max(MinimumColumnWidth, DragStartWidth - Delta);

		if (USizeBox* HeaderBox = GetHeaderBoxFor(DraggingSplitter))
		{
			HeaderBox->SetWidthOverride(*Width);
		}
		BuildList();
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply USWGMissionBrowserWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingDetailsRow)
	{
		bDraggingDetailsRow = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (DraggingSplitter)
	{
		DraggingSplitter = nullptr;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void USWGMissionBrowserWidget::SetSort(ESWGMissionSortField Field)
{
	if (SortField == Field)
	{
		bSortAscending = !bSortAscending;
	}
	else
	{
		SortField = Field;
		bSortAscending = true;
	}
	BuildList();
}

void USWGMissionBrowserWidget::HandleSortByName()       { SetSort(ESWGMissionSortField::Name); }
void USWGMissionBrowserWidget::HandleSortByCost()       { SetSort(ESWGMissionSortField::Cost); }
void USWGMissionBrowserWidget::HandleSortByDifficulty() { SetSort(ESWGMissionSortField::Difficulty); }
void USWGMissionBrowserWidget::HandleSortByDistance()   { SetSort(ESWGMissionSortField::Distance); }
void USWGMissionBrowserWidget::HandleSortByDirection()  { SetSort(ESWGMissionSortField::Direction); }

void USWGMissionBrowserWidget::HandleAcceptClicked()
{
	if (SelectedObjectId == 0)
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	if (USWGMissionSubsystem* Missions_ = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr)
	{
		Missions_->AcceptMission(SelectedObjectId);
	}
}

void USWGMissionBrowserWidget::HandleRefreshClicked()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (USWGMissionSubsystem* Missions_ = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr)
	{
		Missions_->RefreshMissionList();
	}
}

void USWGMissionBrowserWidget::SelectRow(int64 ObjectId)
{
	SelectedObjectId = ObjectId;

	for (const TPair<int64, TObjectPtr<UButton>>& Pair : RowButtons)
	{
		if (UButton* Button = Pair.Value)
		{
			FButtonStyle Style = Button->GetStyle();
			const bool bSelected = Pair.Key == SelectedObjectId;
			Style.Normal.DrawAs = bSelected ? ESlateBrushDrawType::Box : ESlateBrushDrawType::NoDrawType;
			Style.Normal.TintColor = FSlateColor(SelectedRowColor);
			Button->SetStyle(Style);
		}
	}

	ApplyDetails();
}

void USWGMissionBrowserWidget::ApplyDetails()
{
	const FSWGMissionEntry* Selected = Missions.FindByPredicate([this](const FSWGMissionEntry& Entry) { return Entry.ObjectId == SelectedObjectId; });

	if (DetailsTitleText)
	{
		DetailsTitleText->SetText(Selected ? Selected->Title : FText::GetEmpty());
	}
	if (DetailsDescriptionText)
	{
		DetailsDescriptionText->SetText(Selected ? Selected->Description : FText::GetEmpty());
	}
	if (DetailsRewardText)
	{
		DetailsRewardText->SetText(Selected
			? FText::Format(NSLOCTEXT("SWGEmu", "MissionDetailsReward", "Reward: {0} cr   Difficulty: {1}{2}"),
				FText::AsNumber(Selected->RewardCredits), FText::AsNumber(Selected->DifficultyDisplay),
				Selected->TargetTemplateName.IsEmpty() ? FText::GetEmpty() : FText::Format(NSLOCTEXT("SWGEmu", "MissionDetailsTarget", "   Target: {0}"), Selected->TargetTemplateName))
			: FText::GetEmpty());
	}
	if (AcceptButton)
	{
		AcceptButton->SetIsEnabled(Selected != nullptr);
	}

	if (DetailModel)
	{
		DetailModel->ClearModel();
		const int32 TemplateCrc = Selected ? Selected->TargetTemplateCrc : 0;
		if (TemplateCrc != 0)
		{
			USWGItemIconSubsystem* Icons = GetWorld() ? GetWorld()->GetSubsystem<USWGItemIconSubsystem>() : nullptr;
			if (Icons)
			{
				const int64 RequestedFor = SelectedObjectId;
				TWeakObjectPtr<USWGMissionBrowserWidget> WeakThis(this);
				Icons->RequestModelForTemplateCrc(static_cast<uint32>(TemplateCrc), [WeakThis, RequestedFor](UObject* Mesh, const TArray<UMaterialInterface*>& Materials)
				{
					USWGMissionBrowserWidget* Self = WeakThis.Get();
					// The selection can move on before a build finishes.
					if (Self && Self->SelectedObjectId == RequestedFor && Self->DetailModel)
					{
						Self->DetailModel->SetMesh(Mesh, Materials);
					}
				});
			}
		}
	}
}

void USWGMissionBrowserWidget::BuildList()
{
	if (!MissionListPanel)
	{
		return;
	}

	TArray<FSWGMissionEntry> Sorted = Missions.FilterByPredicate([](const FSWGMissionEntry& Entry) { return Entry.bPopulated; });
	Sorted.Sort([this](const FSWGMissionEntry& A, const FSWGMissionEntry& B)
	{
		bool bLess = false;
		switch (SortField)
		{
			case ESWGMissionSortField::Name:       bLess = A.Title.CompareTo(B.Title) < 0; break;
			case ESWGMissionSortField::Cost:       bLess = A.RewardCredits < B.RewardCredits; break;
			case ESWGMissionSortField::Difficulty: bLess = A.DifficultyDisplay < B.DifficultyDisplay; break;
			case ESWGMissionSortField::Distance:   bLess = A.DistanceMeters < B.DistanceMeters; break;
			case ESWGMissionSortField::Direction:  bLess = A.BearingDegrees < B.BearingDegrees; break;
		}
		return bSortAscending ? bLess : !bLess;
	});

	MissionListPanel->ClearChildren();
	RowButtons.Reset();
	RowClickForwarders.Reset();

	for (const FSWGMissionEntry& Entry : Sorted)
	{
		UButton* Row = WidgetTree->ConstructWidget<UButton>();
		FButtonStyle Style = Row->GetStyle();
		Style.Normal.DrawAs = ESlateBrushDrawType::NoDrawType;
		Style.Hovered.DrawAs = ESlateBrushDrawType::Box;
		Style.Hovered.TintColor = FSlateColor(FLinearColor(0.33f, 0.9f, 1.f, 0.15f));
		Style.Pressed.DrawAs = ESlateBrushDrawType::Box;
		Style.Pressed.TintColor = FSlateColor(SelectedRowColor);
		Row->SetStyle(Style);

		UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>();

		// FixedWidth <= 0 means Fill (only the Name column) — everything else gets a
		// SizeBox the same width as its header's, with zero slot padding on both
		// sides (see the header buttons' HorizontalBoxSlots), and the same inset
		// as a header button's own NormalPadding (12, 1.5) applied via a Border
		// here instead — matching structure is what keeps them lined up; matching
		// "roughly the same numbers" doesn't, because slot padding and a button's
		// internal padding are measured from different edges.
		auto AddColumn = [this, RowBox](const FText& Text, float FixedWidth, EHorizontalAlignment Align, const FLinearColor& Color)
		{
			UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
			Block->SetText(Text);
			Block->SetColorAndOpacity(FSlateColor(Color));
			if (RowFont.HasValidFont())
			{
				Block->SetFont(RowFont);
			}

			UBorder* Padded = WidgetTree->ConstructWidget<UBorder>();
			Padded->SetPadding(FMargin(12.f, 1.5f, 12.f, 1.5f));
			Padded->SetHorizontalAlignment(Align);
			Padded->SetContent(Block);
			FSlateBrush NoBrush;
			NoBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
			Padded->SetBrush(NoBrush);

			UWidget* Cell = Padded;
			if (FixedWidth > 0.f)
			{
				// Clip: a value wider than its column (a 6-digit reward, say) would
				// otherwise overflow past the SizeBox and print on top of the next
				// column instead of just being cut off.
				Block->SetClipping(EWidgetClipping::ClipToBounds);
				USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
				Box->SetWidthOverride(FixedWidth);
				Box->AddChild(Padded);
				Cell = Box;
			}

			if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(RowBox->AddChild(Cell)))
			{
				Slot->SetSize(FSlateChildSize(FixedWidth > 0.f ? ESlateSizeRule::Automatic : ESlateSizeRule::Fill));
				Slot->SetHorizontalAlignment(HAlign_Fill);
				Slot->SetVerticalAlignment(VAlign_Fill);
			}
		};

		AddColumn(Entry.Title.IsEmpty() ? NSLOCTEXT("SWGEmu", "MissionUnnamed", "Mission") : Entry.Title, 0.f, HAlign_Left, RowTextColor);
		AddColumn(FText::Format(NSLOCTEXT("SWGEmu", "MissionReward", "{0} cr"), FText::AsNumber(Entry.RewardCredits)), CostColumnWidth, HAlign_Right, RewardTextColor);
		AddColumn(FText::AsNumber(Entry.DifficultyDisplay), DifficultyColumnWidth, HAlign_Right, RowTextColor);
		AddColumn(Entry.bHasStartPosition ? FText::Format(NSLOCTEXT("SWGEmu", "MissionDistance", "{0} m"), FText::AsNumber(FMath::RoundToInt(Entry.DistanceMeters))) : FText::FromString(TEXT("—")),
			DistanceColumnWidth, HAlign_Right, RowTextColor);
		AddColumn(Entry.bHasStartPosition ? FText::FromString(Entry.Direction) : FText::GetEmpty(), DirectionColumnWidth, HAlign_Right, RowTextColor);

		if (UButtonSlot* ButtonContentSlot = Cast<UButtonSlot>(Row->AddChild(RowBox)))
		{
			ButtonContentSlot->SetHorizontalAlignment(HAlign_Fill);
		}

		const int64 ObjectId = Entry.ObjectId;
		USWGMissionRowClickForwarder* Forwarder = NewObject<USWGMissionRowClickForwarder>(this);
		Forwarder->Action = [this, ObjectId]() { SelectRow(ObjectId); };
		Row->OnClicked.AddDynamic(Forwarder, &USWGMissionRowClickForwarder::HandleClicked);
		RowClickForwarders.Add(Forwarder);

		RowButtons.Add(ObjectId, Row);
		MissionListPanel->AddChild(Row);
	}

	// The selection can vanish out from under us — accepted, or the terminal re-randomized the bag.
	if (SelectedObjectId != 0 && !RowButtons.Contains(SelectedObjectId))
	{
		SelectedObjectId = 0;
		ApplyDetails();
	}
	else
	{
		SelectRow(SelectedObjectId);
	}
}
