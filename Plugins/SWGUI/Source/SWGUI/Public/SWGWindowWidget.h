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
 * player can drag by its caption and resize by its bottom-right corner. A Blueprint (WBP_*) lays the chrome out and binds the named widgets.
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

	/** The window can't be dragged smaller than this. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Window")
	FVector2D MinimumSize = FVector2D(240.f, 160.f);

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FCursorReply NativeOnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InCursorEvent) override;

	/** True over the bottom-right corner square that starts a resize. */
	bool IsOverResizeGrip(const FVector2D& ScreenPosition) const;

	/** Current size of the Frame; starts from the Blueprint's overrides. */
	FVector2D WindowSize = FVector2D::ZeroVector;

	/** Where the window's content lives: below the caption, inside the panel. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> Content;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> Caption;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> CloseButton;

	// A Blueprint lays the chrome out itself by binding these: a full-screen
	// canvas holding the Frame (a size box the window moves and resizes),
	// with the Caption and Content inside it.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USizeBox> Frame;

	/** Bottom-right corner glyph; the corner square under it resizes. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UWidget> ResizeGrip;

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
