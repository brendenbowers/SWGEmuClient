#pragma once

#include "CoreMinimal.h"
#include "SWGWindowWidget.h"
#include "Subsystems/SWGExamineSubsystem.h"
#include "SWGExamineWidget.generated.h"

class UModelWidget;
class UTextBlock;

/**
 * Retail's examine window (ui_pda_examine): the item's attributes and
 * description down the left, and the model large on the right, which the
 * player can turn by dragging. Name and description show at once; the
 * attribute lines fill in when the server answers.
 */
UCLASS()
class SWGUI_API USWGExamineWidget : public USWGWindowWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Examine")
	/** False if nothing is known about the object — the window has nothing to show then. */
	bool SetObject(int64 InObjectId);

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Examine")
	int64 GetObjectId() const { return ObjectId; }

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Examine")
	FLinearColor AttributeColor = FLinearColor::FromSRGBColor(FColor(0x96, 0xF4, 0xFC));

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Examine")
	FLinearColor LabelColor = FLinearColor::FromSRGBColor(FColor(0x54, 0xE4, 0xFE));

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Examine")
	int32 FontSize = 13;

	/** Width of the attributes column; the bar between it and the viewer drags to change it. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Examine")
	float DetailsColumnWidth = 220.f;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Examine")
	float MinimumDetailsColumnWidth = 120.f;

	/** The viewer never shrinks below this, whatever the column is dragged to. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Examine")
	float MinimumViewerWidth = 140.f;

	/** Degrees per second the model turns on its own; dragging takes over and it resumes on release. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Examine")
	float TurntableSpeed = 20.f;

	/** Degrees of turntable per pixel of drag. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Examine")
	float DragYawPerPixel = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Examine")
	float DragPitchPerPixel = 0.3f;

protected:
	virtual void BuildContent() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FCursorReply NativeOnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InCursorEvent) override;

	/** True within a few pixels of the bar between the columns. */
	bool IsOverSplitter(const FVector2D& ScreenPosition) const;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UModelWidget> Model;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> AttributePanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DescriptionText;

private:
	UFUNCTION()
	void HandleExamineInfo(const FSWGExamineInfo& Info);

	void Apply(const FSWGExamineInfo& Info);

	/** Sizes the details column; the splitter drags its width override. */
	UPROPERTY()
	TObjectPtr<class USizeBox> DetailsWidthBox;

	/** The bar between the columns. */
	UPROPERTY()
	TObjectPtr<class UBorder> Splitter;

	int64 ObjectId = 0;
	bool bRotating = false;
	bool bSplitting = false;
	float SplitStartWidth = 0.f;
	FVector2D LastDragPosition = FVector2D::ZeroVector;
};
