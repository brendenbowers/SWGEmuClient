#include "SWGWindowWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

void USWGWindowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	// See RootCanvas: the user widget itself spans the screen too.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	CloseButton->OnClicked.AddUniqueDynamic(this, &USWGWindowWidget::HandleCloseClicked);
	TitleText->SetText(Title);
	// The Blueprint's frame carries the window's starting size.
	WindowSize = FVector2D(Frame->GetWidthOverride(), Frame->GetHeightOverride());
	RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (!bPositioned)
	{
		CenterOnScreen();
	}
}

void USWGWindowWidget::SetTitle(const FText& InTitle)
{
	// Retail capitalises captions; the strip is a label, not a sentence.
	Title = FText::FromString(InTitle.ToString().ToUpper());
	TitleText->SetText(Title);
}

void USWGWindowWidget::SetWindowSize(FVector2D InSize)
{
	WindowSize = InSize;
	Frame->SetWidthOverride(WindowSize.X);
	Frame->SetHeightOverride(WindowSize.Y);
}

void USWGWindowWidget::SetWindowPosition(FVector2D InPosition)
{
	bPositioned = true;
	if (UCanvasPanelSlot* FrameSlot = Cast<UCanvasPanelSlot>(Frame->Slot))
	{
		FrameSlot->SetPosition(InPosition);
	}
}

void USWGWindowWidget::CenterOnScreen()
{
	const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), KINDA_SMALL_NUMBER);
	const FVector2D Viewport = UWidgetLayoutLibrary::GetViewportSize(this) / Scale;
	SetWindowPosition((Viewport - WindowSize) * 0.5f);
}

void USWGWindowWidget::Close()
{
	RemoveFromParent();
	OnClosed.Broadcast(this);
}

void USWGWindowWidget::HandleCloseClicked()
{
	Close();
}

bool USWGWindowWidget::IsOverResizeGrip(const FVector2D& ScreenPosition) const
{
	// The grip glyph is small; anything within its corner square counts.
	const FGeometry& FrameGeometry = Frame->GetCachedGeometry();
	const FVector2D LocalMouse = FrameGeometry.AbsoluteToLocal(ScreenPosition);
	const FVector2D LocalSize = FrameGeometry.GetLocalSize();
	return LocalMouse.X >= LocalSize.X - 18.f && LocalMouse.Y >= LocalSize.Y - 18.f
		&& LocalMouse.X <= LocalSize.X && LocalMouse.Y <= LocalSize.Y;
}

FCursorReply USWGWindowWidget::NativeOnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InCursorEvent)
{
	if (bResizing || IsOverResizeGrip(InCursorEvent.GetScreenSpacePosition()))
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
	}
	if (bDragging)
	{
		return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	}
	return Super::NativeOnCursorQuery(InGeometry, InCursorEvent);
}

FReply USWGWindowWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	OnPressed.Broadcast(this);

	const bool bLeft = InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton;
	if (bLeft && IsOverResizeGrip(InMouseEvent.GetScreenSpacePosition()))
	{
		bResizing = true;
		ResizeStartSize = WindowSize;
		ResizeStartMouse = RootCanvas->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget(), EFocusCause::Mouse);
	}

	if (bLeft && Caption->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition()))
	{
		const UCanvasPanelSlot* FrameSlot = Cast<UCanvasPanelSlot>(Frame->Slot);
		const FVector2D MouseInCanvas = RootCanvas->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		DragOffset = MouseInCanvas - (FrameSlot ? FrameSlot->GetPosition() : FVector2D::ZeroVector);
		bDragging = true;
		return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget(), EFocusCause::Mouse);
	}

	// Anything else inside the window is ours too: a click on the panel must
	// not fall through to the world behind it.
	return FReply::Handled().SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

FReply USWGWindowWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging)
	{
		const FVector2D MouseInCanvas = RootCanvas->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		SetWindowPosition(MouseInCanvas - DragOffset);
		return FReply::Handled();
	}
	if (bResizing)
	{
		const FVector2D MouseInCanvas = RootCanvas->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D Size = ResizeStartSize + (MouseInCanvas - ResizeStartMouse);
		SetWindowSize(FVector2D(FMath::Max(Size.X, MinimumSize.X), FMath::Max(Size.Y, MinimumSize.Y)));
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply USWGWindowWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging || bResizing)
	{
		bDragging = false;
		bResizing = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Handled();
}

FReply USWGWindowWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::Gamepad_FaceButton_Right)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
