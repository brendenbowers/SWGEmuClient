#pragma once

#include "CoreMinimal.h"
#include "SWGHoloProjectorActor.h"
#include "SWGHoloShelfActor.generated.h"

class UMeshComponent;

/**
 * The inventory bag as a list of small holograms, a few columns wide, filled
 * row by row and scrolled a row at a time; rows fade in and out at the top and
 * bottom. The owner lays the grid out on screen each frame (SetGridFrame) and
 * draws the names; this draws the models, fitted to one size, turning slowly.
 * Models come from USWGItemIconSubsystem.
 */
UCLASS(NotPlaceable)
class SWGUI_API ASWGHoloShelfActor : public ASWGHoloProjectorActor
{
	GENERATED_BODY()

public:
	ASWGHoloShelfActor();

	/** The bag's items in display order; models already built for an item are kept. */
	void SetItems(const TArray<int64>& ObjectIds);
	const TArray<int64>& GetItems() const { return ItemIds; }

	/**
	 * Where the grid is this frame, world space: the middle of the top-left
	 * cell's model, the step to the next column and to the next row down, and
	 * how big a model may be.
	 */
	void SetGridFrame(const FVector& TopLeft, const FVector& ColumnStep, const FVector& RowStep, float InItemSize);

	/** Columns and rows showing at once; set before SetItems. */
	void SetGridSize(int32 InColumns, int32 InVisibleRows);
	int32 GetColumns() const { return Columns; }

	/** Scrolls by whole rows; clamped so the list never runs past its ends. */
	void Scroll(int32 Rows);
	/** Scrolls until the item's row is showing. */
	void ScrollTo(int64 ObjectId);

	/** The hovered or selected item turns faster and glows; zero for none. */
	void SetHovered(int64 ObjectId);

	/** Shows or hides every model, as when another pane takes the list's place. */
	void SetShelfShown(bool bShown) { bShelfShown = bShown; }

	/**
	 * Asked for each model's world centre every layout; true hides it, as a
	 * scan wipes past. Asked here rather than handed a list, because the
	 * scene draws before widgets tick and a list would trail by a frame.
	 */
	void SetItemMask(TFunction<bool(const FVector&)> InItemMask) { ItemMask = MoveTemp(InItemMask); }

	/** Middle of an item's model, world space; false if its row isn't showing. */
	bool GetItemCenter(int64 ObjectId, FVector& OutWorld) const;

	/** Items whose rows are showing, in reading order. */
	TArray<int64> GetVisibleItems() const;

	/** How many items lie in the rows above and below what is showing. */
	int32 CountHiddenBefore() const;
	int32 CountHiddenAfter() const;

	/** Rows per second the list slides to catch up with a scroll. */
	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	float ScrollSpeed = 10.f;

	UPROPERTY(EditAnywhere, Category = "SWGEmu|Hologram")
	float SpinDegreesPerSecond = 25.f;

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual float GetFadeRadius() const override { return 100000.f; }

private:
	struct FShelfItem
	{
		int64 ObjectId = 0;
		TObjectPtr<USceneComponent> Pivot;
		TObjectPtr<UMeshComponent> Model;
		TObjectPtr<UMaterialInstanceDynamic> Material;
		/** The model's own size, so it can be refitted when the cell size changes. */
		float ModelExtent = 0.f;
		FVector ModelOrigin = FVector::ZeroVector;
		float Spin = 0.f;
		float Glow = 0.f;
	};

	void AddItem(int64 ObjectId);
	void RequestModel(int64 ObjectId);
	void AttachModel(int64 ObjectId, UObject* Mesh);
	void FitModel(FShelfItem& Item) const;
	void Layout(float DeltaSeconds);
	/** Where item Index sits, actor space, at the current scroll. */
	FVector CellLocation(int32 Index) const;
	/** 1 for a showing row, fading to 0 a row past either end. */
	float RowVisibility(int32 Index) const;
	int32 IndexOf(int64 ObjectId) const;
	int32 MaxScrollRow() const;

	TArray<int64> ItemIds;
	bool bShelfShown = true;
	TFunction<bool(const FVector&)> ItemMask;
	TArray<FShelfItem> Items;

	/** Keeps the per-item components and materials alive; FShelfItem isn't reflected. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> ItemObjects;

	int32 Columns = 2;
	int32 VisibleRows = 6;
	float ColumnSpacing = 20.f;
	float RowSpacing = 10.f;
	float ItemSize = 6.f;

	/** The top showing row wanted, and where the slide has got to. */
	float TargetScrollRow = 0.f;
	float CurrentScrollRow = 0.f;
	int64 HoveredId = 0;
};
