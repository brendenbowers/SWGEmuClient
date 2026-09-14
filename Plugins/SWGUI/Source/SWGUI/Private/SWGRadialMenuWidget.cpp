#include "SWGRadialMenuWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "CommonInputSubsystem.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Objects/Player/SWGPlayer.h"

void USWGRadialMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
}

void USWGRadialMenuWidget::Open(const FSWGRadialMenu& Menu)
{
	CurrentMenu = Menu;

	if (UCanvasPanelSlot* PanelSlot = MenuPanel ? Cast<UCanvasPanelSlot>(MenuPanel->Slot) : nullptr)
	{
		FVector2D Position = Menu.ScreenPosition;
		if (GEngine && GEngine->GameViewport)
		{
			// Screen pixels -> slate units, then keep the menu on screen.
			FVector2D ViewportSize;
			GEngine->GameViewport->GetViewportSize(ViewportSize);
			const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), KINDA_SMALL_NUMBER);
			Position /= Scale;
			ViewportSize /= Scale;
			Position.X = FMath::Clamp(Position.X, 0.f, FMath::Max(0.f, ViewportSize.X - EstimatedMenuSize.X));
			Position.Y = FMath::Clamp(Position.Y, 0.f, FMath::Max(0.f, ViewportSize.Y - EstimatedMenuSize.Y));
		}
		PanelSlot->SetPosition(Position);
		PanelSlot->SetAutoSize(true);
	}

	ShowLevel(0);

	// A gamepad has no cursor to hover with, so start it on the first row.
	const UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(GetOwningLocalPlayer());
	if (CommonInput && CommonInput->GetCurrentInputType() == ECommonInputType::Gamepad)
	{
		SetHighlightedRow(0);
	}

	SetFocus();
}

void USWGRadialMenuWidget::Close()
{
	RemoveFromParent();
}

FReply USWGRadialMenuWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Only the backdrop gets here — rows are buttons and eat their own clicks.
	Close();
	return FReply::Handled();
}

FReply USWGRadialMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// The button that opened the menu closes it again.
	const ASWGPlayer* Player = Cast<ASWGPlayer>(GetOwningPlayerPawn());
	if (Key == EKeys::Escape || (Player && Key == Player->InteractKey))
	{
		Close();
		return FReply::Handled();
	}

	if (Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up)
	{
		MoveHighlight(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down)
	{
		MoveHighlight(1);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		ActivateHighlightedRow();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Right)
	{
		Back();
		return FReply::Handled();
	}

	// Swallow the rest of the D-pad/face buttons so they don't fire action
	// bar slots underneath the open menu.
	if (Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right
		|| Key == EKeys::Gamepad_FaceButton_Left || Key == EKeys::Gamepad_FaceButton_Top)
	{
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void USWGRadialMenuWidget::ShowLevel(int32 ParentIndex)
{
	if (!ItemBox)
	{
		return;
	}

	CurrentParentIndex = ParentIndex;
	ItemBox->ClearChildren();
	Rows.Reset();
	RowButtons.Reset();

	// Keep a gamepad highlight through a level change; a mouse user has none.
	const bool bHadHighlight = HighlightedRow != INDEX_NONE;
	HighlightedRow = INDEX_NONE;

	if (ParentIndex != 0)
	{
		const FSWGRadialMenuItem* Parent = CurrentMenu.Items.FindByPredicate([ParentIndex](const FSWGRadialMenuItem& Item) { return Item.Index == ParentIndex; });
		const int32 GrandparentIndex = Parent ? Parent->ParentIndex : 0;
		AddRow(FText::FromString(FString(TEXT("◂ Back"))), [this, GrandparentIndex]() { ShowLevel(GrandparentIndex); });
	}

	for (const FSWGRadialMenuItem& Item : CurrentMenu.Items)
	{
		if (Item.ParentIndex != ParentIndex)
		{
			continue;
		}

		if (CurrentMenu.HasChildren(Item.Index))
		{
			const int32 ChildIndex = Item.Index;
			AddRow(FText::FromString(Item.Label.ToString() + TEXT(" ▸")), [this, ChildIndex]() { ShowLevel(ChildIndex); });
		}
		else
		{
			const int64 ObjectId = CurrentMenu.ObjectId;
			const int32 RadialId = Item.RadialId;
			AddRow(Item.Label, [this, ObjectId, RadialId]()
			{
				UGameInstance* GameInstance = GetGameInstance();
				if (USWGRadialMenuSubsystem* Radial = GameInstance ? GameInstance->GetSubsystem<USWGRadialMenuSubsystem>() : nullptr)
				{
					Radial->SelectOption(ObjectId, RadialId);
				}
				Close();
			});
		}
	}

	if (bHadHighlight)
	{
		// Skip the Back row on a submenu so the highlight lands on a real option.
		SetHighlightedRow(ParentIndex != 0 && RowButtons.Num() > 1 ? 1 : 0);
	}
}

void USWGRadialMenuWidget::AddRow(const FText& Label, TFunction<void()> OnClicked)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetText(Label);
	Text->SetColorAndOpacity(FSlateColor(RowTextColor));
	if (RowFont.HasValidFont())
	{
		Text->SetFont(RowFont);
	}

	// Flat rows: no button chrome at rest, the retail cyan wash when hovered/pressed.
	FButtonStyle Style = Button->GetStyle();
	Style.Normal.DrawAs = ESlateBrushDrawType::NoDrawType;
	Style.Hovered.DrawAs = ESlateBrushDrawType::Box;
	Style.Hovered.TintColor = FSlateColor(FLinearColor(0.33f, 0.9f, 1.f, 0.25f));
	Style.Pressed.DrawAs = ESlateBrushDrawType::Box;
	Style.Pressed.TintColor = FSlateColor(FLinearColor(0.33f, 0.9f, 1.f, 0.4f));
	Style.NormalPadding = RowPadding;
	Style.PressedPadding = RowPadding;
	Button->SetStyle(Style);

	if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Button->AddChild(Text)))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Left);
	}

	USWGRadialMenuRow* Row = NewObject<USWGRadialMenuRow>(this);
	Row->Action = MoveTemp(OnClicked);
	Button->OnClicked.AddDynamic(Row, &USWGRadialMenuRow::HandleClicked);
	Rows.Add(Row);
	RowButtons.Add(Button);

	ItemBox->AddChild(Button);
}

void USWGRadialMenuWidget::SetHighlightedRow(int32 RowIndex)
{
	HighlightedRow = RowButtons.IsValidIndex(RowIndex) ? RowIndex : INDEX_NONE;

	const UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(GetOwningLocalPlayer());
	const bool bGamepad = CommonInput && CommonInput->GetCurrentInputType() == ECommonInputType::Gamepad;

	// The highlighted row wears the hover wash at rest, and on a gamepad only
	// it does — the idle mouse pointer must not paint a second one.
	for (int32 ButtonIndex = 0; ButtonIndex < RowButtons.Num(); ++ButtonIndex)
	{
		UButton* Button = RowButtons[ButtonIndex];
		if (!Button)
		{
			continue;
		}

		FButtonStyle Style = Button->GetStyle();
		Style.Normal.DrawAs = ButtonIndex == HighlightedRow ? ESlateBrushDrawType::Box : ESlateBrushDrawType::NoDrawType;
		Style.Normal.TintColor = Style.Hovered.TintColor;
		Style.Hovered.DrawAs = bGamepad ? ESlateBrushDrawType::NoDrawType : ESlateBrushDrawType::Box;
		Button->SetStyle(Style);
	}
}

void USWGRadialMenuWidget::MoveHighlight(int32 Direction)
{
	if (RowButtons.IsEmpty())
	{
		return;
	}

	if (HighlightedRow == INDEX_NONE)
	{
		SetHighlightedRow(0);
		return;
	}

	SetHighlightedRow((HighlightedRow + Direction + RowButtons.Num()) % RowButtons.Num());
}

void USWGRadialMenuWidget::ActivateHighlightedRow()
{
	if (HighlightedRow == INDEX_NONE)
	{
		SetHighlightedRow(0);
		return;
	}

	if (USWGRadialMenuRow* Row = Rows.IsValidIndex(HighlightedRow) ? Rows[HighlightedRow].Get() : nullptr)
	{
		Row->HandleClicked();
	}
}

void USWGRadialMenuWidget::Back()
{
	if (CurrentParentIndex == 0)
	{
		Close();
		return;
	}

	const int32 ParentIndex = CurrentParentIndex;
	const FSWGRadialMenuItem* Parent = CurrentMenu.Items.FindByPredicate([ParentIndex](const FSWGRadialMenuItem& Item) { return Item.Index == ParentIndex; });
	ShowLevel(Parent ? Parent->ParentIndex : 0);
}
