#pragma once

#include "CoreMinimal.h"
#include "SWGWindowWidget.h"
#include "Subsystems/SWGMissionSubsystem.h"
#include "SWGMissionBrowserWidget.generated.h"

class UTextBlock;
class UPanelWidget;
class UButton;
class USizeBox;
class UModelWidget;

/** Which column the mission list is currently sorted by. */
UENUM(BlueprintType)
enum class ESWGMissionSortField : uint8
{
	Name,
	Cost,
	Difficulty,
	Distance,
	Direction,
};

/**
 * The mission terminal's browser window — opened by USWGUISubsystem on
 * USWGMissionSubsystem::OnMissionWindowRequested, refreshed on
 * OnMissionListChanged. A floating USWGWindowWidget like Inventory/Examine,
 * not a modal SUI page — matching retail's ui_mission.inc "Browser" page,
 * which is itself UserMovable/UserResizable. Not a SUI window either way:
 * see FMissionListRequest for why mission terminals don't use the generic
 * server-UI path.
 *
 * Retail's Browser page (ui_mission.inc) is a sortable table (type/start/
 * end/bounty/payment/owner) with Accept/Details/Refresh/Exit buttons and a
 * separate Details page as the preview pane. This mirrors that shape with
 * what our decode currently exposes: a sortable Name/Cost/Distance list,
 * a details pane for the current selection, and Accept/Refresh buttons
 * (Exit/Close comes from the shared window chrome already).
 */
UCLASS(Abstract)
class SWGUI_API USWGMissionBrowserWidget : public USWGWindowWidget
{
	GENERATED_BODY()

public:
	void SetMissions(const TArray<FSWGMissionEntry>& InMissions);

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FCursorReply NativeOnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InCursorEvent) override;

	/** Holds the mission rows, built in code. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> MissionListPanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> NameHeaderButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CostHeaderButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> DifficultyHeaderButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> DistanceHeaderButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> DirectionHeaderButton;

	// The SizeBox each header button sits in, so a splitter drag can push its
	// WidthOverride live — see the matching column-width floats below, which
	// are what BuildList reads for the rows.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> CostHeaderBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> DifficultyHeaderBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> DistanceHeaderBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> DirectionHeaderBox;

	/** Thin drag handles at each resizable column's left edge. Optional: a column with no splitter bound just can't be resized. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CostSplitter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> DifficultySplitter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> DistanceSplitter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> DirectionSplitter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailsTitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailsDescriptionText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailsRewardText;

	/** The selected mission's target (lair/camp/NPC) drawn live — see USWGItemIconSubsystem::RequestModelForTemplateCrc. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UModelWidget> DetailModel;

	/** The SizeBoxes a details-row drag pushes a shared height into — see DetailsRowHeight. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> DetailsDescriptionBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> DetailModelBox;

	/** Drag handle below the description/model row, for resizing DetailsRowHeight. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> DetailsRowSplitter;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> AcceptButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> RefreshButton;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission")
	FSlateFontInfo RowFont;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission")
	FLinearColor RowTextColor = FLinearColor::FromSRGBColor(FColor(0x96, 0xF4, 0xFC));

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission")
	FLinearColor RewardTextColor = FLinearColor::FromSRGBColor(FColor(0x37, 0xFD, 0x06));

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission")
	FLinearColor SelectedRowColor = FLinearColor(0.33f, 0.9f, 1.f, 0.35f);

	// Fixed widths so the header and every row cell line up in columns —
	// Name is the only one left to Fill the remaining space.
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission|Columns")
	float CostColumnWidth = 110.f;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission|Columns")
	float DifficultyColumnWidth = 90.f;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission|Columns")
	float DistanceColumnWidth = 100.f;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission|Columns")
	float DirectionColumnWidth = 60.f;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission|Columns")
	float MinimumColumnWidth = 40.f;

	/** Height of the description/model row — both DetailsDescriptionBox and DetailModelBox share this, dragged live via DetailsRowSplitter. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission")
	float DetailsRowHeight = 260.f;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission")
	float MinimumDetailsRowHeight = 60.f;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission")
	float MaximumDetailsRowHeight = 500.f;

private:
	UFUNCTION() void HandleSortByName();
	UFUNCTION() void HandleSortByCost();
	UFUNCTION() void HandleSortByDifficulty();
	UFUNCTION() void HandleSortByDistance();
	UFUNCTION() void HandleSortByDirection();
	UFUNCTION() void HandleAcceptClicked();
	UFUNCTION() void HandleRefreshClicked();

	void SetSort(ESWGMissionSortField Field);
	void SelectRow(int64 ObjectId);
	void BuildList();
	void ApplyDetails();

	/** The splitter (if any) whose left edge is under ScreenPosition. */
	UWidget* FindSplitterUnderMouse(const FVector2D& ScreenPosition) const;
	/** The column-width float a splitter drags, or nullptr. */
	float* GetColumnWidthFor(UWidget* Splitter);
	/** The header SizeBox whose WidthOverride a splitter drag should update live. */
	USizeBox* GetHeaderBoxFor(UWidget* Splitter) const;

	TArray<FSWGMissionEntry> Missions;
	TMap<int64, TObjectPtr<UButton>> RowButtons;

	ESWGMissionSortField SortField = ESWGMissionSortField::Distance;
	bool bSortAscending = true;
	int64 SelectedObjectId = 0;

	TObjectPtr<UWidget> DraggingSplitter = nullptr;
	float DragStartWidth = 0.f;
	FVector2D DragStartMouse = FVector2D::ZeroVector;

	/** True under the DetailsRowSplitter's hit region — a few px either side, like FindSplitterUnderMouse but for one vertical bar. */
	bool IsOverDetailsRowSplitter(const FVector2D& ScreenPosition) const;
	void ApplyDetailsRowHeight();

	bool bDraggingDetailsRow = false;
	float DetailsRowDragStartHeight = 0.f;
	FVector2D DetailsRowDragStartMouse = FVector2D::ZeroVector;

	UPROPERTY()
	TArray<TObjectPtr<class USWGMissionRowClickForwarder>> RowClickForwarders;
};

/** Click target for a code-built row button — UButton::OnClicked is a dynamic delegate, which can't bind a lambda directly. */
UCLASS()
class USWGMissionRowClickForwarder : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void()> Action;

	UFUNCTION()
	void HandleClicked() { if (Action) { Action(); } }
};
