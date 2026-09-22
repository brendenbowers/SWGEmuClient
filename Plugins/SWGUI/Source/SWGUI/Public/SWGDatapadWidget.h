#pragma once

#include "CoreMinimal.h"
#include "SWGWindowWidget.h"
#include "SWGInventoryQuery.h"
#include "SWGDatapadWidget.generated.h"

class UTextBlock;
class UPanelWidget;
class USWGInventoryRowWidget;

/**
 * The datapad window: schematics, mission items, deeds and other intangible
 * objects the player's datapad bag holds, read straight from the object
 * graph's containment — same idiom as USWGInventoryWidget (which this
 * mirrors closely), just one panel instead of an equipped/bag split, since
 * nothing in the datapad is worn. WBP_Datapad lays it out; rows are
 * WBP_InventoryRow instances, reused as-is; right-clicking a row opens its
 * radial menu.
 */
UCLASS(Abstract)
class SWGUI_API USWGDatapadWidget : public USWGWindowWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Datapad")
	void Refresh();

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Datapad")
	TArray<FSWGInventoryEntry> GetContents() const { return Contents; }

	/** Fired after a refresh that changed something. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SWGEmu|Datapad")
	void OnDatapadUpdated();

	/** Closes on this as well as Escape, so the key that opened it also shuts it. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Datapad")
	FKey ToggleKey = EKeys::B;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** Containment changes arrive as separate messages with no "done" — poll instead of chasing each. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Datapad")
	float RefreshInterval = 0.5f;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> DatapadHeader;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> ContentsPanel;

	/** Shown in place of the rows when the list is empty. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UWidget> ContentsEmptyText;

private:
	void BuildRows();

	/** Left selects; right asks the server for the item's radial menu, same as right-clicking it in the world. */
	void HandleRowPressed(int64 ObjectId, FKey Button, FVector2D ScreenPosition);

	TArray<FSWGInventoryEntry> Contents;

	UPROPERTY()
	TArray<TObjectPtr<USWGInventoryRowWidget>> Rows;

	int64 SelectedObjectId = 0;

	FTimerHandle RefreshTimer;
};
