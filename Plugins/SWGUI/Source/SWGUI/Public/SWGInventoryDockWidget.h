#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGInventoryQuery.h"
#include "Subsystems/SWGExamineSubsystem.h"
#include "SWGInventoryDockWidget.generated.h"

class UModelWidget;
class UPanelWidget;
class UTextBlock;
class USWGInventoryRowWidget;

UENUM(BlueprintType)
enum class ESWGInventoryTab : uint8
{
	Equipped,
	Inventory,
	/** Objects examined out in the world; the tab appears once there is one. */
	Examine,
};

/**
 * The gamepad inventory (WBP_InventoryDock): a panel docked to one edge of
 * the screen rather than a floating window, in the console-RPG idiom — tabs
 * for gear, bag and examined world objects switched with the shoulder
 * buttons, one list walked with the D-pad or stick, and the focused item's
 * model, name and attributes shown alongside so there is no separate examine
 * step. Menu or A opens the item's actions (the radial menu), B closes.
 * While open it owns the D-pad and face buttons, so the action bar's hotkeys
 * don't fire underneath it.
 */
UCLASS(Abstract)
class SWGUI_API USWGInventoryDockWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Inventory")
	void Refresh();

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Inventory")
	void SetTab(ESWGInventoryTab InTab);

	/** Steps to the previous (-1) or next (+1) available tab. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Inventory")
	void CycleTab(int32 Direction);

	/** Moves the cursor by Delta rows, clamped; details follow. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Inventory")
	void MoveCursor(int32 Delta);

	/**
	 * Puts an object examined out in the world at the top of the Examine tab
	 * and shows that tab, so a world examine reads like an inventory item.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Inventory")
	void AddExamined(int64 ObjectId);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Inventory")
	void Close();

	/** Takes keyboard/gamepad focus back — after the radial menu it opened goes away. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Inventory")
	void Refocus();

	FSimpleMulticastDelegate OnClosed;

	/** Containment changes arrive as separate messages with no "done" — poll instead of chasing each. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	float RefreshInterval = 0.5f;

	/** How many examined world objects the Examine tab keeps. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	int32 ExamineHistory = 12;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	FLinearColor ActiveTabColor = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	FLinearColor InactiveTabColor = FLinearColor(0.45f, 0.45f, 0.45f);

	/** Degrees per second the detail model turns. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory|Details")
	float TurntableSpeed = 20.f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent) override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> EquippedTabText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> InventoryTabText;

	/** Collapsed until something has been examined. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> ExamineTabText;

	/** The rows of the active tab. A scroll box, so the cursor can be kept in view. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> ListPanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UWidget> ListEmptyText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> DetailNameText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UModelWidget> DetailModel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> DetailAttributePanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> DetailDescriptionText;

private:
	void RebuildList();
	void ApplyCursor();
	void ShowDetails(int64 ObjectId);
	void OpenActions();
	void ApplyTabStyle();

	UFUNCTION()
	void HandleExamineInfo(const FSWGExamineInfo& Info);

	void HandleRowPressed(int64 ObjectId, FKey Button, FVector2D ScreenPosition);

	const TArray<FSWGInventoryEntry>& ActiveEntries() const;

	/** Which tabs are on offer right now — Examine only once something has been examined. */
	TArray<ESWGInventoryTab> AvailableTabs() const;

	TArray<FSWGInventoryEntry> Equipped;
	TArray<FSWGInventoryEntry> Contents;
	/** Objects examined out in the world, newest first. */
	TArray<FSWGInventoryEntry> Examined;

	UPROPERTY()
	TArray<TObjectPtr<USWGInventoryRowWidget>> Rows;

	ESWGInventoryTab Tab = ESWGInventoryTab::Inventory;
	/** Row under the cursor. */
	int32 Cursor = 0;
	/** What the details column is showing, so a late server answer for something else is ignored. */
	int64 DetailObjectId = 0;

	FTimerHandle RefreshTimer;
};
