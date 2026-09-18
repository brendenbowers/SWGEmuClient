#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGWindowWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UPanelWidget;
class USizeBox;
class UTextBlock;
class USWGWindowWidget;

DECLARE_MULTICAST_DELEGATE_OneParam(FSWGOnWindowClosed, USWGWindowWidget*);

/**
 * A floating in-game window in the retail idiom — rounded translucent panel,
 * cyan outline, a caption strip with the title and a close button — that the
 * player can drag by its caption and resize by its bottom-right corner. Subclasses fill Content in BuildContent().
 * Lives on the player screen (USWGUISubsystem adds and stacks them), so any
 * number can be open at once; Escape closes the focused one.
 */
UCLASS(Abstract)
class SWGUI_API USWGWindowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Window")
	void SetTitle(const FText& InTitle);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Window")
	void SetWindowSize(FVector2D InSize);

	/** Top-left corner in viewport (slate) units. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Window")
	void SetWindowPosition(FVector2D InPosition);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Window")
	void CenterOnScreen();

	/** Removes the window and tells whoever opened it. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Window")
	void Close();

	FSWGOnWindowClosed OnClosed;

	/** Fired on any press inside the window, so the owner can raise it above its siblings. */
	FSWGOnWindowClosed OnPressed;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Window")
	FVector2D WindowSize = FVector2D(420.f, 560.f);

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Window")
	float CornerRadius = 8.f;

	/** Retail's back1 / line1 / titletext palette entries. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Window")
	FLinearColor PanelColor = FLinearColor(0.008f, 0.06f, 0.09f, 0.9f);

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Window")
	FLinearColor OutlineColor = FLinearColor::FromSRGBColor(FColor(0x1C, 0xFF, 0xFF, 0x99));

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Window")
	FLinearColor CaptionColor = FLinearColor::FromSRGBColor(FColor(0x00, 0xD6, 0xFB, 0x8C));

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Window")
	FLinearColor TitleTextColor = FLinearColor::FromSRGBColor(FColor(0x00, 0x35, 0x4F));

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Window")
	float CaptionHeight = 24.f;

	/** The window can't be dragged smaller than this. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Window")
	FVector2D MinimumSize = FVector2D(240.f, 160.f);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FCursorReply NativeOnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InCursorEvent) override;

	/** True over the bottom-right corner square that starts a resize. */
	bool IsOverResizeGrip(const FVector2D& ScreenPosition) const;

	/** Fill Content with the window's own widgets. Runs once, after the chrome exists. */
	virtual void BuildContent() {}

	/** Where subclasses put their widgets: a vertical box inside the panel, below the caption. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> Content;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Caption;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	// A Blueprint lays the chrome out itself by binding these: a full-screen
	// canvas holding the Frame (a size box the window moves and resizes),
	// with the Caption and Content inside it.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> Frame;

	/** Bottom-right corner glyph; the corner square under it resizes. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> ResizeGrip;

	/** True when the code-built chrome is in use rather than a Blueprint's. */
	bool bNativeChrome = false;

private:
	UFUNCTION()
	void HandleCloseClicked();

	FText Title;
	bool bDragging = false;
	/** Mouse position relative to the frame's corner when the drag began, in canvas units. */
	FVector2D DragOffset = FVector2D::ZeroVector;
	bool bPositioned = false;
	bool bResizing = false;
	FVector2D ResizeStartSize = FVector2D::ZeroVector;
	FVector2D ResizeStartMouse = FVector2D::ZeroVector;
};
