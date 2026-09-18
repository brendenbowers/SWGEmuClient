#include "SWGWindowWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/SlateTypes.h"

namespace
{
	FSlateBrush MakeRoundedBrush(FLinearColor Fill, FLinearColor Outline, FVector4 Radii, float OutlineWidth)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings = FSlateBrushOutlineSettings(Radii, FSlateColor(Outline), OutlineWidth);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		return Brush;
	}

	/** A button with no chrome of its own: the glyph inside is the whole control. */
	FButtonStyle MakeBareButtonStyle()
	{
		FButtonStyle Style;
		Style.Normal = FSlateNoResource();
		Style.Hovered = FSlateNoResource();
		Style.Pressed = FSlateNoResource();
		Style.NormalPadding = FMargin(0.f);
		Style.PressedPadding = FMargin(0.f);
		return Style;
	}
}

TSharedRef<SWidget> USWGWindowWidget::RebuildWidget()
{
	// Only when no Blueprint tree exists — a subclass with its own layout
	// binds the named widgets instead.
	if (!WidgetTree->RootWidget)
	{
		RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = RootCanvas;

		Frame = WidgetTree->ConstructWidget<USizeBox>();
		Frame->SetWidthOverride(WindowSize.X);
		Frame->SetHeightOverride(WindowSize.Y);
		UCanvasPanelSlot* FrameSlot = RootCanvas->AddChildToCanvas(Frame);
		FrameSlot->SetAutoSize(true);

		// Only the frame takes hits: the canvas spans the whole screen, and a
		// hit-testable one would swallow clicks meant for the world and for
		// windows underneath.
		RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		UOverlay* Layers = WidgetTree->ConstructWidget<UOverlay>();
		Frame->AddChild(Layers);

		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrush(MakeRoundedBrush(PanelColor, OutlineColor, FVector4(CornerRadius, CornerRadius, CornerRadius, CornerRadius), 1.f));
		Panel->SetPadding(FMargin(0.f));
		UOverlaySlot* PanelSlot = Layers->AddChildToOverlay(Panel);
		PanelSlot->SetHorizontalAlignment(HAlign_Fill);
		PanelSlot->SetVerticalAlignment(VAlign_Fill);

		UTextBlock* Grip = WidgetTree->ConstructWidget<UTextBlock>();
		Grip->SetText(FText::FromString(TEXT("\u25E2")));
		Grip->SetColorAndOpacity(FSlateColor(OutlineColor));
		FSlateFontInfo GripFont = Grip->GetFont();
		GripFont.Size = 10;
		Grip->SetFont(GripFont);
		ResizeGrip = Grip;
		UOverlaySlot* GripSlot = Layers->AddChildToOverlay(Grip);
		GripSlot->SetHorizontalAlignment(HAlign_Right);
		GripSlot->SetVerticalAlignment(VAlign_Bottom);
		GripSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 2.f));

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		Panel->AddChild(Column);

		// Caption: title on the left, close glyph on the right, rounded to
		// match the panel's top corners.
		Caption = WidgetTree->ConstructWidget<UBorder>();
		Caption->SetBrush(MakeRoundedBrush(CaptionColor, FLinearColor::Transparent, FVector4(CornerRadius, CornerRadius, 0.f, 0.f), 0.f));
		Caption->SetPadding(FMargin(10.f, 0.f, 6.f, 0.f));
		UVerticalBoxSlot* CaptionSlot = Column->AddChildToVerticalBox(Caption);
		CaptionSlot->SetHorizontalAlignment(HAlign_Fill);

		USizeBox* CaptionHeightBox = WidgetTree->ConstructWidget<USizeBox>();
		CaptionHeightBox->SetHeightOverride(CaptionHeight);
		Caption->AddChild(CaptionHeightBox);

		UHorizontalBox* CaptionRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		CaptionHeightBox->AddChild(CaptionRow);

		TitleText = WidgetTree->ConstructWidget<UTextBlock>();
		TitleText->SetColorAndOpacity(FSlateColor(TitleTextColor));
		FSlateFontInfo TitleFont = TitleText->GetFont();
		TitleFont.Size = 12;
		TitleFont.TypefaceFontName = TEXT("Bold");
		TitleFont.LetterSpacing = 60;
		TitleText->SetFont(TitleFont);
		UHorizontalBoxSlot* TitleSlot = CaptionRow->AddChildToHorizontalBox(TitleText);
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Center);

		CloseButton = WidgetTree->ConstructWidget<UButton>();
		CloseButton->SetStyle(MakeBareButtonStyle());
		UTextBlock* CloseGlyph = WidgetTree->ConstructWidget<UTextBlock>();
		CloseGlyph->SetText(FText::FromString(TEXT("×")));
		CloseGlyph->SetColorAndOpacity(FSlateColor(TitleTextColor));
		FSlateFontInfo GlyphFont = CloseGlyph->GetFont();
		GlyphFont.Size = 16;
		CloseGlyph->SetFont(GlyphFont);
		CloseButton->AddChild(CloseGlyph);
		UHorizontalBoxSlot* CloseSlot = CaptionRow->AddChildToHorizontalBox(CloseButton);
		CloseSlot->SetVerticalAlignment(VAlign_Center);
		CloseSlot->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));

		UVerticalBox* ContentBox = WidgetTree->ConstructWidget<UVerticalBox>();
		Content = ContentBox;
		UVerticalBoxSlot* ContentSlot = Column->AddChildToVerticalBox(ContentBox);
		ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ContentSlot->SetPadding(FMargin(12.f, 10.f, 12.f, 12.f));

		bNativeChrome = true;
		BuildContent();
	}
	return Super::RebuildWidget();
}

void USWGWindowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	// See RootCanvas: the user widget itself spans the screen too.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (CloseButton)
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &USWGWindowWidget::HandleCloseClicked);
	}
	if (TitleText)
	{
		TitleText->SetText(Title);
	}
	// A Blueprint's frame carries its own size; the code path sized it from WindowSize.
	if (!bNativeChrome && Frame && Frame->GetWidthOverride() > 0.f && Frame->GetHeightOverride() > 0.f)
	{
		WindowSize = FVector2D(Frame->GetWidthOverride(), Frame->GetHeightOverride());
	}
	if (RootCanvas)
	{
		RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (!bPositioned)
	{
		CenterOnScreen();
	}
}

void USWGWindowWidget::SetTitle(const FText& InTitle)
{
	// Retail capitalises captions; the strip is a label, not a sentence.
	Title = FText::FromString(InTitle.ToString().ToUpper());
	if (TitleText)
	{
		TitleText->SetText(Title);
	}
}

void USWGWindowWidget::SetWindowSize(FVector2D InSize)
{
	WindowSize = InSize;
	if (Frame)
	{
		Frame->SetWidthOverride(WindowSize.X);
		Frame->SetHeightOverride(WindowSize.Y);
	}
}

void USWGWindowWidget::SetWindowPosition(FVector2D InPosition)
{
	bPositioned = true;
	if (UCanvasPanelSlot* FrameSlot = Frame ? Cast<UCanvasPanelSlot>(Frame->Slot) : nullptr)
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
	if (!Frame)
	{
		return false;
	}
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
	if (bLeft && RootCanvas && IsOverResizeGrip(InMouseEvent.GetScreenSpacePosition()))
	{
		bResizing = true;
		ResizeStartSize = WindowSize;
		ResizeStartMouse = RootCanvas->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget(), EFocusCause::Mouse);
	}

	if (bLeft && Caption && Frame && RootCanvas
		&& Caption->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition()))
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
	if (bDragging && RootCanvas)
	{
		const FVector2D MouseInCanvas = RootCanvas->GetCachedGeometry().AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		SetWindowPosition(MouseInCanvas - DragOffset);
		return FReply::Handled();
	}
	if (bResizing && RootCanvas)
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
