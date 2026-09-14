#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Subsystems/SWGSuiSubsystem.h"
#include "SWGSuiBoxWidget.generated.h"

class UTextBlock;
class UButton;
class UEditableTextBox;
class UPanelWidget;
class UWidget;

/**
 * A server UI window: covers retail's messageBox, listBox and inputBox
 * templates with one layout, showing only the parts the page uses. Answers
 * go back through USWGSuiSubsystem::Respond with the field values the page
 * subscribed to (selected row, typed text, which button).
 *
 * Bind what the Blueprint provides; anything missing is skipped:
 *   TitleText, PromptText, OkButton(+OkLabel), CancelButton(+CancelLabel),
 *   OtherButton(+OtherLabel), InputBox, ListPanel (rows built in code).
 *
 * Gamepad: D-pad up/down moves the list selection, A is OK, B is Cancel
 * (when the page shows one), X is the third button.
 */
UCLASS(Abstract)
class SWGUI_API USWGSuiBoxWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	void SetPage(const FSWGSuiPage& InPage);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|SUI")
	int32 GetPageId() const { return Page.PageId; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** The input box when the page has one, else the window itself, so gamepad keys reach NativeOnKeyDown on activation. */
	virtual UWidget* NativeGetDesiredFocusTarget() const override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PromptText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> OkButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> OkLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CancelLabel;

	/** Retail's third button: "btnRevert" on a message box, "btnOther" on a list box. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> OtherButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> OtherLabel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> InputBox;

	/** Holds the list rows; hidden for pages without a list. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ListPanel;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|SUI")
	FSlateFontInfo RowFont;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|SUI")
	FLinearColor RowTextColor = FLinearColor::FromSRGBColor(FColor(0x54, 0xE4, 0xFE));

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|SUI")
	FLinearColor SelectedRowColor = FLinearColor(0.33f, 0.9f, 1.f, 0.35f);

private:
	UFUNCTION() void HandleOk();
	UFUNCTION() void HandleCancel();
	UFUNCTION() void HandleOther();

	void Apply();
	void BuildList();
	void SelectRow(int32 RowIndex);

	/** Steps the list selection with wrap; from nothing selected, lands on the first (or last) row. */
	void MoveSelection(int32 Direction);

	void Submit(int32 EventType, bool bOtherPressed);

	/** Current "widget.property" values the page may ask for. */
	TMap<FString, FString> CollectValues(bool bOtherPressed) const;

	FSWGSuiPage Page;
	bool bPageSet = false;
	bool bAnswered = false;
	int32 SelectedRow = INDEX_NONE;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> RowButtons;

	UPROPERTY()
	TArray<TObjectPtr<class USWGSuiListRow>> Rows;
};

/** Click target for a code-built list row. */
UCLASS()
class USWGSuiListRow : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void()> Action;

	UFUNCTION()
	void HandleClicked() { if (Action) { Action(); } }
};
