#include "SWGExamineWidget.h"
#include "ModelWidget.h"
#include "SWGExamineLines.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"

void USWGExamineWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Model->SetRotateSpeed(TurntableSpeed);
	DetailsColumnWidth = DetailsWidthBox->GetWidthOverride();

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
	Model->SetObject(ObjectId);
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

	DescriptionText->SetText(FText::FromString(Info.Description));
	DescriptionText->SetVisibility(Info.Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);

	SWGExamineLines::Fill(this, AttributePanel, Info);
}

bool USWGExamineWidget::IsOverSplitter(const FVector2D& ScreenPosition) const
{
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

	if (bLeft && IsOverSplitter(InMouseEvent.GetScreenSpacePosition()))
	{
		OnPressed.Broadcast(this);
		bSplitting = true;
		SplitStartWidth = DetailsColumnWidth;
		LastDragPosition = InMouseEvent.GetScreenSpacePosition();
		return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget(), EFocusCause::Mouse);
	}

	if (bLeft && Model->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition()))
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
	if (bSplitting)
	{
		// Screen pixels to slate units, so the column tracks the cursor at any DPI.
		const float Scale = FMath::Max(InGeometry.Scale, KINDA_SMALL_NUMBER);
		const float Delta = (InMouseEvent.GetScreenSpacePosition().X - LastDragPosition.X) / Scale;
		const float ViewerLimit = WindowSize.X - MinimumViewerWidth;
		DetailsColumnWidth = FMath::Clamp(SplitStartWidth + Delta, MinimumDetailsColumnWidth, FMath::Max(MinimumDetailsColumnWidth, ViewerLimit));
		DetailsWidthBox->SetWidthOverride(DetailsColumnWidth);
		return FReply::Handled();
	}

	if (bRotating)
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
		Model->SetRotateSpeed(TurntableSpeed);
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}
