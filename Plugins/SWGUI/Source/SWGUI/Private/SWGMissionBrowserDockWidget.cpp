#include "SWGMissionBrowserDockWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "ModelWidget.h"
#include "Subsystems/SWGItemIconSubsystem.h"
#include "Engine/GameInstance.h"

namespace
{
	bool IsCursorUp(const FKey& Key)   { return Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up || Key == EKeys::Up; }
	bool IsCursorDown(const FKey& Key) { return Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down || Key == EKeys::Down; }

	bool IsOwnedGamepadKey(const FKey& Key)
	{
		return Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Up || Key == EKeys::Gamepad_LeftStick_Down
			|| Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Gamepad_FaceButton_Top || Key == EKeys::Gamepad_FaceButton_Right
			|| Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_FaceButton_Left;
	}
}

void USWGMissionBrowserDockWidget::SetMissions(const TArray<FSWGMissionEntry>& InMissions)
{
	Missions = InMissions.FilterByPredicate([](const FSWGMissionEntry& Entry) { return Entry.bPopulated; });
	Cursor = FMath::Clamp(Cursor, 0, FMath::Max(0, Missions.Num() - 1));
	if (IsConstructed())
	{
		RebuildList();
	}
}

void USWGMissionBrowserDockWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RebuildList();
}

void USWGMissionBrowserDockWidget::Close()
{
	RemoveFromParent();
	OnClosed.Broadcast();
}

FReply USWGMissionBrowserDockWidget::NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent)
{
	return FReply::Handled();
}

FReply USWGMissionBrowserDockWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
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
	if (Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Enter)
	{
		Accept();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Top)
	{
		Refresh();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Escape)
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

FReply USWGMissionBrowserDockWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	return IsOwnedGamepadKey(InKeyEvent.GetKey()) ? FReply::Handled() : Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

void USWGMissionBrowserDockWidget::MoveCursor(int32 Delta)
{
	if (Missions.IsEmpty())
	{
		return;
	}
	Cursor = (Cursor + Delta + Missions.Num()) % Missions.Num();
	ApplyCursor();
}

void USWGMissionBrowserDockWidget::Accept()
{
	if (!Missions.IsValidIndex(Cursor))
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	if (USWGMissionSubsystem* MissionSubsystem = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr)
	{
		MissionSubsystem->AcceptMission(Missions[Cursor].ObjectId);
	}
}

void USWGMissionBrowserDockWidget::Refresh()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (USWGMissionSubsystem* MissionSubsystem = GameInstance ? GameInstance->GetSubsystem<USWGMissionSubsystem>() : nullptr)
	{
		MissionSubsystem->RefreshMissionList();
	}
}

void USWGMissionBrowserDockWidget::RebuildList()
{
	if (!ListPanel)
	{
		return;
	}

	ListPanel->ClearChildren();
	RowButtons.Reset();

	if (ListEmptyText)
	{
		ListEmptyText->SetVisibility(Missions.IsEmpty() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	for (const FSWGMissionEntry& Entry : Missions)
	{
		UButton* Row = WidgetTree->ConstructWidget<UButton>();
		FButtonStyle Style = Row->GetStyle();
		Style.Normal.DrawAs = ESlateBrushDrawType::NoDrawType;
		Style.Hovered.DrawAs = ESlateBrushDrawType::Box;
		Style.Hovered.TintColor = FSlateColor(FLinearColor(0.33f, 0.9f, 1.f, 0.15f));
		Row->SetStyle(Style);

		UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>();

		auto AddCell = [this, RowBox](const FText& Text, float FixedWidth, EHorizontalAlignment Align)
		{
			UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
			Block->SetText(Text);
			Block->SetColorAndOpacity(FSlateColor(RowTextColor));
			Block->SetClipping(EWidgetClipping::ClipToBounds);

			UWidget* Cell = Block;
			if (FixedWidth > 0.f)
			{
				USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
				Box->SetWidthOverride(FixedWidth);
				Box->AddChild(Block);
				Cell = Box;
			}
			if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(RowBox->AddChild(Cell)))
			{
				Slot->SetSize(FSlateChildSize(FixedWidth > 0.f ? ESlateSizeRule::Automatic : ESlateSizeRule::Fill));
				Slot->SetHorizontalAlignment(Align);
				Slot->SetPadding(FMargin(6.f, 3.f));
			}
		};

		AddCell(Entry.Title.IsEmpty() ? NSLOCTEXT("SWGEmu", "MissionUnnamed", "Mission") : Entry.Title, 0.f, HAlign_Left);
		AddCell(FText::Format(NSLOCTEXT("SWGEmu", "MissionReward", "{0} cr"), FText::AsNumber(Entry.RewardCredits)), 90.f, HAlign_Right);
		AddCell(Entry.bHasStartPosition ? FText::Format(NSLOCTEXT("SWGEmu", "MissionDistanceDir", "{0} m {1}"), FText::AsNumber(FMath::RoundToInt(Entry.DistanceMeters)), FText::FromString(Entry.Direction)) : FText::FromString(TEXT("—")),
			110.f, HAlign_Right);

		if (UButtonSlot* ButtonContentSlot = Cast<UButtonSlot>(Row->AddChild(RowBox)))
		{
			ButtonContentSlot->SetHorizontalAlignment(HAlign_Fill);
		}

		RowButtons.Add(Row);
		ListPanel->AddChild(Row);
	}

	ApplyCursor();
}

void USWGMissionBrowserDockWidget::ApplyCursor()
{
	for (int32 Index = 0; Index < RowButtons.Num(); ++Index)
	{
		if (UButton* Button = RowButtons[Index])
		{
			FButtonStyle Style = Button->GetStyle();
			const bool bCursor = Index == Cursor;
			Style.Normal.DrawAs = bCursor ? ESlateBrushDrawType::Box : ESlateBrushDrawType::NoDrawType;
			Style.Normal.TintColor = FSlateColor(CursorRowColor);
			Button->SetStyle(Style);
		}
	}

	if (UScrollBox* Scroll = Cast<UScrollBox>(ListPanel); Scroll && RowButtons.IsValidIndex(Cursor))
	{
		Scroll->ScrollWidgetIntoView(RowButtons[Cursor], true);
	}

	const FSWGMissionEntry* Selected = Missions.IsValidIndex(Cursor) ? &Missions[Cursor] : nullptr;
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

	if (DetailModel)
	{
		DetailModel->ClearModel();
		const int32 TemplateCrc = Selected ? Selected->TargetTemplateCrc : 0;
		if (TemplateCrc != 0)
		{
			USWGItemIconSubsystem* Icons = GetWorld() ? GetWorld()->GetSubsystem<USWGItemIconSubsystem>() : nullptr;
			if (Icons)
			{
				const int64 RequestedFor = Selected->ObjectId;
				TWeakObjectPtr<USWGMissionBrowserDockWidget> WeakThis(this);
				Icons->RequestModelForTemplateCrc(static_cast<uint32>(TemplateCrc), [WeakThis, RequestedFor](UObject* Mesh, const TArray<UMaterialInterface*>& Materials)
				{
					USWGMissionBrowserDockWidget* Self = WeakThis.Get();
					const FSWGMissionEntry* Current = Self && Self->Missions.IsValidIndex(Self->Cursor) ? &Self->Missions[Self->Cursor] : nullptr;
					if (Self && Current && Current->ObjectId == RequestedFor && Self->DetailModel)
					{
						Self->DetailModel->SetMesh(Mesh, Materials);
					}
				});
			}
		}
	}
}
