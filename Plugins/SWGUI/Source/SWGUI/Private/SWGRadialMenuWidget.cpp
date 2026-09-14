#include "SWGRadialMenuWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"

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
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Close();
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

	ItemBox->AddChild(Button);
}
