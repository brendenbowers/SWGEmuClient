#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "SWGRadialMenuWidget.generated.h"

class UPanelWidget;
class UButton;
class UTextBlock;
class UWidget;

/**
 * The object context menu, opened by right-clicking an object. Retail draws
 * a pie; this is a list at the click point with drill-down submenus (a parent
 * option replaces the list with its children plus a Back row), which also
 * suits a gamepad later.
 *
 * Designer layout: a full-viewport root that catches clicks to dismiss, with
 * MenuPanel (any widget) positioned at the click and ItemBox (a vertical
 * panel) inside it. Rows are built in code from RowButtonClass or, when
 * unset, a plain button + text.
 */
UCLASS(Abstract)
class SWGUI_API USWGRadialMenuWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Shows Menu at its ScreenPosition. */
	void Open(const FSWGRadialMenu& Menu);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Radial")
	void Close();

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** Positioned at the click point; the rows live inside it. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> MenuPanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> ItemBox;

	/** Row look for code-built rows. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Radial")
	FSlateFontInfo RowFont;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Radial")
	FLinearColor RowTextColor = FLinearColor::FromSRGBColor(FColor(0x54, 0xE4, 0xFE));

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Radial")
	FMargin RowPadding = FMargin(10.f, 4.f);

	/** Keep the menu from opening off the bottom/right edge. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Radial")
	FVector2D EstimatedMenuSize = FVector2D(180.f, 220.f);

private:
	/** Rebuilds the rows for the options under ParentIndex (0 = top level). */
	void ShowLevel(int32 ParentIndex);
	void AddRow(const FText& Label, TFunction<void()> OnClicked);

	FSWGRadialMenu CurrentMenu;
	int32 CurrentParentIndex = 0;

	UPROPERTY()
	TArray<TObjectPtr<class USWGRadialMenuRow>> Rows;
};

/** A row's click target — UButton::OnClicked needs a UFUNCTION, and each row wants its own action. */
UCLASS()
class USWGRadialMenuRow : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void()> Action;

	UFUNCTION()
	void HandleClicked() { if (Action) { Action(); } }
};
