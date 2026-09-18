#pragma once

#include "CoreMinimal.h"
#include "SWGWindowWidget.h"
#include "SWGInventoryWidget.generated.h"

class UTextBlock;
class UPanelWidget;
class USWGInventoryRowWidget;

/** One row of the window, flattened for Blueprint. */
USTRUCT(BlueprintType)
struct SWGUI_API FSWGInventoryEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Inventory")
	int64 ObjectId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Inventory")
	FString Name;

	/** The equip slots it fills ("chest1", "hold_r"), joined with commas. Empty for a bag item. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Inventory")
	FString SlotNames;

	/** Stack size for resources, otherwise 0. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Inventory")
	int32 Quantity = 0;

	bool operator==(const FSWGInventoryEntry& Other) const
	{
		return ObjectId == Other.ObjectId && Name == Other.Name && SlotNames == Other.SlotNames && Quantity == Other.Quantity;
	}
};

/**
 * The inventory window: what the player is wearing and wielding, and what is
 * in their inventory bag, read straight from the object graph's containment.
 * WBP_Inventory lays it out; rows are WBP_InventoryRow instances, each with
 * the item's live model; right-clicking a row opens its radial menu. No drag and drop yet.
 */
UCLASS(Abstract)
class SWGUI_API USWGInventoryWidget : public USWGWindowWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Inventory")
	void Refresh();

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Inventory")
	TArray<FSWGInventoryEntry> GetEquipped() const { return Equipped; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Inventory")
	TArray<FSWGInventoryEntry> GetContents() const { return Contents; }

	/** Fired after a refresh that changed something. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SWGEmu|Inventory")
	void OnInventoryUpdated();

	/** Closes on this as well as Escape, so the key that opened it also shuts it. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	FKey ToggleKey = EKeys::I;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** Containment changes arrive as separate messages with no "done" — poll instead of chasing each. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Inventory")
	float RefreshInterval = 0.5f;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> InventoryHeader;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> EquippedPanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> InventoryPanel;

	/** Shown in place of the rows when the list is empty. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UWidget> EquippedEmptyText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UWidget> InventoryEmptyText;

private:
	/** Reads the player's containment tree into Equipped and Contents. True if either list changed. */
	bool Gather();

	FSWGInventoryEntry DescribeObject(int64 ObjectId) const;

	void BuildRows(UPanelWidget* Panel, UWidget* EmptyLabel, const TArray<FSWGInventoryEntry>& Entries);

	/** Left selects; right asks the server for the item's radial menu, same as right-clicking it in the world. */
	void HandleRowPressed(int64 ObjectId, FKey Button, FVector2D ScreenPosition);

	/** The player's inventory bag — the volume container under the creature whose template is character_inventory. */
	int64 FindInventoryBagId(int64 PlayerId) const;

	TArray<FSWGInventoryEntry> Equipped;
	TArray<FSWGInventoryEntry> Contents;

	UPROPERTY()
	TArray<TObjectPtr<USWGInventoryRowWidget>> Rows;

	int64 SelectedObjectId = 0;

	FTimerHandle RefreshTimer;
};
