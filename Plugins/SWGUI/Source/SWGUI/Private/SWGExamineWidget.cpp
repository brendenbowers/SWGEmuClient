#include "SWGExamineWidget.h"
#include "ModelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"

namespace
{
	UTextBlock* MakeText(UWidgetTree* Tree, int32 Size, FLinearColor Color)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetAutoWrapText(true);
		FSlateFontInfo Font = Block->GetFont();
		Font.Size = Size;
		Block->SetFont(Font);
		return Block;
	}
}

void USWGExamineWidget::BuildContent()
{
	UVerticalBox* Column = Cast<UVerticalBox>(Content);
	if (!Column)
	{
		return;
	}

	// Retail: 557 x 395, a 220-wide details column, the viewer taking the rest.
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(Row)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	DetailsWidthBox = WidgetTree->ConstructWidget<USizeBox>();
	DetailsWidthBox->SetWidthOverride(DetailsColumnWidth);
	Row->AddChildToHorizontalBox(DetailsWidthBox);

	UScrollBox* Details = WidgetTree->ConstructWidget<UScrollBox>();
	DetailsWidthBox->AddChild(Details);

	// A thin bar between the columns; dragging it trades width between them.
	UBorder* SplitterBar = WidgetTree->ConstructWidget<UBorder>();
	SplitterBar->SetBrushColor(FLinearColor(0.11f, 1.f, 1.f, 0.25f));
	SplitterBar->SetPadding(FMargin(0.f));
	USizeBox* SplitterWidth = WidgetTree->ConstructWidget<USizeBox>();
	SplitterWidth->SetWidthOverride(3.f);
	SplitterBar->AddChild(SplitterWidth);
	Splitter = SplitterBar;
	UHorizontalBoxSlot* SplitterSlot = Row->AddChildToHorizontalBox(SplitterBar);
	SplitterSlot->SetPadding(FMargin(4.f, 8.f, 4.f, 8.f));
	SplitterSlot->SetVerticalAlignment(VAlign_Fill);

	UVerticalBox* Attributes = WidgetTree->ConstructWidget<UVerticalBox>();
	AttributePanel = Attributes;
	Details->AddChild(Attributes);

	DescriptionText = MakeText(WidgetTree, FontSize, AttributeColor);
	UBorder* DescriptionFrame = WidgetTree->ConstructWidget<UBorder>();
	DescriptionFrame->SetBrushColor(FLinearColor(1.f, 1.f, 1.f, 0.06f));
	DescriptionFrame->SetPadding(FMargin(6.f));
	DescriptionFrame->AddChild(DescriptionText);
	Details->AddChild(DescriptionFrame);
	if (UScrollBoxSlot* DescriptionSlot = Cast<UScrollBoxSlot>(DescriptionFrame->Slot))
	{
		DescriptionSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
	}

	UBorder* ViewerFrame = WidgetTree->ConstructWidget<UBorder>();
	ViewerFrame->SetBrushColor(FLinearColor(0.f, 0.84f, 0.98f, 0.08f));
	ViewerFrame->SetPadding(FMargin(4.f));
	UHorizontalBoxSlot* ViewerSlot = Row->AddChildToHorizontalBox(ViewerFrame);
	ViewerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	Model = WidgetTree->ConstructWidget<UModelWidget>();
	Model->Fill = 0.8f;
	Model->ViewRotation = FRotator(-12.f, 135.f, 0.f);
	Model->RotateSpeed = TurntableSpeed;
	ViewerFrame->AddChild(Model);
}

void USWGExamineWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (bNativeChrome)
	{
		SetWindowSize(FVector2D(557.f, 395.f));
	}
	if (Model)
	{
		Model->SetRotateSpeed(TurntableSpeed);
	}
	if (DetailsWidthBox && !bNativeChrome && DetailsWidthBox->GetWidthOverride() > 0.f)
	{
		DetailsColumnWidth = DetailsWidthBox->GetWidthOverride();
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USWGExamineSubsystem* Examine = GameInstance->GetSubsystem<USWGExamineSubsystem>())
		{
			Examine->OnExamineInfo.AddUniqueDynamic(this, &USWGExamineWidget::HandleExamineInfo);
		}
	}
}

void USWGExamineWidget::NativeDestruct()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USWGExamineSubsystem* Examine = GameInstance->GetSubsystem<USWGExamineSubsystem>())
		{
			Examine->OnExamineInfo.RemoveDynamic(this, &USWGExamineWidget::HandleExamineInfo);
		}
	}
	Super::NativeDestruct();
}

bool USWGExamineWidget::SetObject(int64 InObjectId)
{
	ObjectId = InObjectId;

	UGameInstance* GameInstance = GetGameInstance();
	USWGExamineSubsystem* Examine = GameInstance ? GameInstance->GetSubsystem<USWGExamineSubsystem>() : nullptr;
	FSWGExamineInfo Info;
	if (!Examine || !Examine->Describe(ObjectId, Info))
	{
		return false;
	}
	Apply(Info);
	if (Model)
	{
		Model->SetObject(ObjectId);
	}
	return true;
}

void USWGExamineWidget::HandleExamineInfo(const FSWGExamineInfo& Info)
{
	if (Info.ObjectId == ObjectId)
	{
		Apply(Info);
	}
}

void USWGExamineWidget::Apply(const FSWGExamineInfo& Info)
{
	SetTitle(FText::FromString(Info.Name));

	if (DescriptionText)
	{
		DescriptionText->SetText(FText::FromString(Info.Description));
		DescriptionText->SetVisibility(Info.Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	if (AttributePanel && !Info.Attributes.IsEmpty())
	{
		AttributePanel->ClearChildren();
		FString LastCategory;
		for (const FSWGExamineAttribute& Attribute : Info.Attributes)
		{
			// Retail prints the group once, above its lines, when it changes.
			if (Attribute.Category != LastCategory)
			{
				LastCategory = Attribute.Category;
				if (!LastCategory.IsEmpty())
				{
					UTextBlock* Header = MakeText(WidgetTree, FontSize, FLinearColor::White);
					Header->SetText(FText::FromString(LastCategory));
					if (UVerticalBoxSlot* HeaderSlot = Cast<UVerticalBoxSlot>(AttributePanel->AddChild(Header)))
					{
						HeaderSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 2.f));
					}
				}
			}

			UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();

			UTextBlock* Label = MakeText(WidgetTree, FontSize, LabelColor);
			Label->SetText(FText::FromString(Attribute.Label + TEXT(":")));
			Label->SetAutoWrapText(false);
			UHorizontalBoxSlot* LabelSlot = Line->AddChildToHorizontalBox(Label);
			LabelSlot->SetPadding(FMargin(Attribute.Category.IsEmpty() ? 0.f : 10.f, 1.f, 8.f, 1.f));

			UTextBlock* Value = MakeText(WidgetTree, FontSize, AttributeColor);
			Value->SetText(FText::FromString(Attribute.Value));
			UHorizontalBoxSlot* ValueSlot = Line->AddChildToHorizontalBox(Value);
			ValueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			ValueSlot->SetPadding(FMargin(0.f, 1.f));

			AttributePanel->AddChild(Line);
		}
	}
}

bool USWGExamineWidget::IsOverSplitter(const FVector2D& ScreenPosition) const
{
	if (!Splitter)
	{
		return false;
	}
	// The bar is 3 px wide; accept a few pixels either side of it.
	const FGeometry& SplitterGeometry = Splitter->GetCachedGeometry();
	const FVector2D LocalMouse = SplitterGeometry.AbsoluteToLocal(ScreenPosition);
	const FVector2D LocalSize = SplitterGeometry.GetLocalSize();
	return LocalMouse.X >= -5.f && LocalMouse.X <= LocalSize.X + 5.f && LocalMouse.Y >= 0.f && LocalMouse.Y <= LocalSize.Y;
}

FCursorReply USWGExamineWidget::NativeOnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InCursorEvent)
{
	if (bSplitting || (!bRotating && !IsOverResizeGrip(InCursorEvent.GetScreenSpacePosition()) && IsOverSplitter(InCursorEvent.GetScreenSpacePosition())))
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}
	if (bRotating)
	{
		return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	}
	return Super::NativeOnCursorQuery(InGeometry, InCursorEvent);
}

FReply USWGExamineWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const bool bLeft = InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton;

	if (bLeft && DetailsWidthBox && IsOverSplitter(InMouseEvent.GetScreenSpacePosition()))
	{
		OnPressed.Broadcast(this);
		bSplitting = true;
		SplitStartWidth = DetailsColumnWidth;
		LastDragPosition = InMouseEvent.GetScreenSpacePosition();
		return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget(), EFocusCause::Mouse);
	}

	if (bLeft && Model
		&& Model->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition()))
	{
		OnPressed.Broadcast(this);
		bRotating = true;
		LastDragPosition = InMouseEvent.GetScreenSpacePosition();
		Model->SetRotateSpeed(0.f);
		return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget(), EFocusCause::Mouse);
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply USWGExamineWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bSplitting && DetailsWidthBox)
	{
		// Screen pixels to slate units, so the column tracks the cursor at any DPI.
		const float Scale = FMath::Max(InGeometry.Scale, KINDA_SMALL_NUMBER);
		const float Delta = (InMouseEvent.GetScreenSpacePosition().X - LastDragPosition.X) / Scale;
		const float ViewerLimit = WindowSize.X - MinimumViewerWidth;
		DetailsColumnWidth = FMath::Clamp(SplitStartWidth + Delta, MinimumDetailsColumnWidth, FMath::Max(MinimumDetailsColumnWidth, ViewerLimit));
		DetailsWidthBox->SetWidthOverride(DetailsColumnWidth);
		return FReply::Handled();
	}

	if (bRotating && Model)
	{
		const FVector2D Delta = InMouseEvent.GetScreenSpacePosition() - LastDragPosition;
		LastDragPosition = InMouseEvent.GetScreenSpacePosition();

		Model->SetYaw(Model->GetYaw() + Delta.X * DragYawPerPixel);
		FRotator View = Model->ViewRotation;
		View.Pitch -= Delta.Y * DragPitchPerPixel;
		Model->SetViewRotation(View);
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply USWGExamineWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bSplitting)
	{
		bSplitting = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (bRotating)
	{
		bRotating = false;
		if (Model)
		{
			Model->SetRotateSpeed(TurntableSpeed);
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}
