#include "SWGSuiBoxWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"

namespace
{
	const FString ListSource = TEXT("List.dataList");

	void SetTextIfBound(UTextBlock* Block, const FString& Value)
	{
		if (Block)
		{
			Block->SetText(FText::FromString(Value));
		}
	}

	void ShowIfBound(UWidget* Widget, bool bShow)
	{
		if (Widget)
		{
			Widget->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}
}

void USWGSuiBoxWidget::SetPage(const FSWGSuiPage& InPage)
{
	Page = InPage;
	bPageSet = true;
	bAnswered = false;
	SelectedRow = INDEX_NONE;
	if (IsConstructed())
	{
		Apply();
	}
}

void USWGSuiBoxWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (OkButton)
	{
		OkButton->OnClicked.AddUniqueDynamic(this, &USWGSuiBoxWidget::HandleOk);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.AddUniqueDynamic(this, &USWGSuiBoxWidget::HandleCancel);
	}
	if (OtherButton)
	{
		OtherButton->OnClicked.AddUniqueDynamic(this, &USWGSuiBoxWidget::HandleOther);
	}

	if (bPageSet)
	{
		Apply();
	}
}

void USWGSuiBoxWidget::NativeDestruct()
{
	// Closed some other way (layer cleared, server force-close): treat as cancel so the server isn't left waiting.
	if (bPageSet && !bAnswered)
	{
		Submit(SWGSuiEvent::Cancel, false);
	}
	Super::NativeDestruct();
}

void USWGSuiBoxWidget::Apply()
{
	SetTextIfBound(TitleText, Page.GetProperty(TEXT("bg.caption.lblTitle"), TEXT("Text")));
	SetTextIfBound(PromptText, Page.GetProperty(TEXT("Prompt.lblPrompt"), TEXT("Text")));

	auto ButtonText = [this](const TCHAR* Widget, const FText& Fallback)
	{
		const FString Text = Page.GetProperty(Widget, TEXT("Text"));
		return Text.IsEmpty() ? Fallback : FText::FromString(Text);
	};

	if (OkLabel)
	{
		OkLabel->SetText(ButtonText(TEXT("btnOk"), NSLOCTEXT("SWGEmu", "SuiOk", "OK")));
	}

	// A cancel button is present unless the page hid it (message boxes without one say Visible=False).
	const bool bCancel = Page.GetPropertyBool(TEXT("btnCancel"), TEXT("Visible"), true) && Page.GetPropertyBool(TEXT("btnCancel"), TEXT("visible"), true);
	ShowIfBound(CancelButton, bCancel);
	if (CancelLabel)
	{
		CancelLabel->SetText(ButtonText(TEXT("btnCancel"), NSLOCTEXT("SWGEmu", "SuiCancel", "Cancel")));
	}

	// Third button: btnRevert on message boxes, btnOther on list boxes; both default hidden.
	const bool bRevert = Page.GetPropertyBool(TEXT("btnRevert"), TEXT("Visible"), false);
	const bool bOther = Page.GetPropertyBool(TEXT("btnOther"), TEXT("visible"), false);
	ShowIfBound(OtherButton, bRevert || bOther);
	if (OtherLabel)
	{
		OtherLabel->SetText(bRevert ? ButtonText(TEXT("btnRevert"), NSLOCTEXT("SWGEmu", "SuiOther", "Other"))
								   : ButtonText(TEXT("btnOther"), NSLOCTEXT("SWGEmu", "SuiOther", "Other")));
	}

	const bool bInput = Page.ScriptClass.Contains(TEXT("inputBox")) || Page.GetPropertyBool(TEXT("txtInput"), TEXT("Visible"), false);
	ShowIfBound(InputBox, bInput);
	if (InputBox && bInput)
	{
		InputBox->SetText(FText::FromString(Page.GetProperty(TEXT("txtInput"), TEXT("Text"))));
		InputBox->SetFocus();
	}

	const FSWGSuiStringList* List = Page.Lists.Find(ListSource);
	ShowIfBound(ListPanel, List != nullptr);
	if (List)
	{
		BuildList();
	}
}

void USWGSuiBoxWidget::BuildList()
{
	if (!ListPanel)
	{
		return;
	}

	ListPanel->ClearChildren();
	RowButtons.Reset();
	Rows.Reset();

	const FSWGSuiStringList& List = Page.Lists.FindChecked(ListSource);
	for (int32 RowIndex = 0; RowIndex < List.Entries.Num(); ++RowIndex)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
		Text->SetText(FText::FromString(List.Entries[RowIndex]));
		Text->SetColorAndOpacity(FSlateColor(RowTextColor));
		if (RowFont.HasValidFont())
		{
			Text->SetFont(RowFont);
		}

		FButtonStyle Style = Button->GetStyle();
		Style.Normal.DrawAs = ESlateBrushDrawType::NoDrawType;
		Style.Hovered.DrawAs = ESlateBrushDrawType::Box;
		Style.Hovered.TintColor = FSlateColor(FLinearColor(0.33f, 0.9f, 1.f, 0.2f));
		Style.Pressed.DrawAs = ESlateBrushDrawType::Box;
		Style.Pressed.TintColor = FSlateColor(SelectedRowColor);
		Style.NormalPadding = FMargin(8.f, 3.f);
		Style.PressedPadding = FMargin(8.f, 3.f);
		Button->SetStyle(Style);

		if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Button->AddChild(Text)))
		{
			ContentSlot->SetHorizontalAlignment(HAlign_Left);
		}

		USWGSuiListRow* Row = NewObject<USWGSuiListRow>(this);
		Row->Action = [this, RowIndex]() { SelectRow(RowIndex); };
		Button->OnClicked.AddDynamic(Row, &USWGSuiListRow::HandleClicked);
		Rows.Add(Row);
		RowButtons.Add(Button);
		ListPanel->AddChild(Button);
	}
}

void USWGSuiBoxWidget::SelectRow(int32 RowIndex)
{
	SelectedRow = RowIndex;
	for (int32 ButtonIndex = 0; ButtonIndex < RowButtons.Num(); ++ButtonIndex)
	{
		if (UButton* Button = RowButtons[ButtonIndex])
		{
			FButtonStyle Style = Button->GetStyle();
			Style.Normal.DrawAs = ButtonIndex == RowIndex ? ESlateBrushDrawType::Box : ESlateBrushDrawType::NoDrawType;
			Style.Normal.TintColor = FSlateColor(SelectedRowColor);
			Button->SetStyle(Style);
		}
	}
}

TMap<FString, FString> USWGSuiBoxWidget::CollectValues(bool bOtherPressed) const
{
	TMap<FString, FString> Values;
	Values.Add(TEXT("List.lstList.selectedrow"), FString::FromInt(SelectedRow));
	Values.Add(TEXT("txtInput.localtext"), InputBox ? InputBox->GetText().ToString() : FString());
	Values.Add(TEXT("cmbInput.selectedtext"), FString());
	Values.Add(TEXT("this.otherpressed"), bOtherPressed ? TEXT("true") : TEXT("false"));
	// Echo the page's own values for anything else it asked back for (titles, MaxLength, ...).
	for (const TPair<FString, FString>& Property : Page.Properties)
	{
		Values.FindOrAdd(Property.Key, Property.Value);
	}
	return Values;
}

void USWGSuiBoxWidget::Submit(int32 EventType, bool bOtherPressed)
{
	if (bAnswered)
	{
		return;
	}
	bAnswered = true;

	UGameInstance* GameInstance = GetGameInstance();
	USWGSuiSubsystem* Sui = GameInstance ? GameInstance->GetSubsystem<USWGSuiSubsystem>() : nullptr;
	FSWGSuiPage StillOpen;
	if (Sui && Sui->GetOpenPage(Page.PageId, StillOpen))
	{
		// Not open any more means the server already closed it — nothing to answer.
		Sui->Respond(Page.PageId, EventType, CollectValues(bOtherPressed));
	}
}

void USWGSuiBoxWidget::HandleOk()
{
	Submit(SWGSuiEvent::Ok, false);
	DeactivateWidget();
}

void USWGSuiBoxWidget::HandleCancel()
{
	Submit(SWGSuiEvent::Cancel, false);
	DeactivateWidget();
}

void USWGSuiBoxWidget::HandleOther()
{
	// Retail treats the third button as OK with this.otherPressed set.
	Submit(SWGSuiEvent::Ok, true);
	DeactivateWidget();
}
