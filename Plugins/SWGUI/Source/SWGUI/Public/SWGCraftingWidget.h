#pragma once

#include "CoreMinimal.h"
#include "SWGWindowWidget.h"
#include "SWGCraftingWidget.generated.h"

class UButton;
class UModelWidget;
class UPanelWidget;
class UTextBlock;
class UVerticalBox;
class USWGCraftingSubsystem;
class USWGTreSubsystem;
class USWGExamineSubsystem;
struct FSWGExamineInfo;
enum class ESWGCraftingSessionState : uint8;
enum class ESWGCraftingSlotResult : uint8;
enum class ESWGCraftingResult : uint8;

/**
 * The crafting tool window: the Draft page (after retail ui_craft_draft.inc
 * — tab bar, schematic list, browse-preview details pane) and the Assembly
 * page (required slots + inventory candidates to fill them), and the
 * Experiment page. They share the same window/Content column; only one
 * is visible at a time. Later stages add Customize/Summary here too.
 */
UCLASS(Abstract)
class SWGUI_API USWGCraftingWidget : public USWGWindowWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ── Draft page ──────────────────────────────────────────────────────

	/** One button per distinct ToolTab bit seen in the session's schematic list. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> TabBar;

	/** Draft page's root — everything under it is shown only at ESWGCraftingSessionState::ChoosingSchematic. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UWidget> Body;

	/**
	 * A category tree of the selected tab's schematics, not a flat list —
	 * rows are either an expand/collapse category header (from the
	 * schematic's real TRE folder path, e.g. object/draft_schematic/weapon/
	 * component/..., since retail's own skill-based draft tree isn't
	 * data-driven — see crafting-protocol.md) or an indented leaf schematic.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> SchematicList;

	/** Required-slot names for the highlighted schematic (browse preview, not yet committed). */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> SlotsList;

	/** Resource weight rows for the highlighted schematic. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> WeightsList;

	/** Commits the highlighted schematic (SelectSchematic) — retail's "Create Item" first step. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> SelectButton;

	/** Optional: the highlighted schematic's authored name over the details pane. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SchematicNameText;

	/** Optional: session state / hints ("Select a schematic", "Assembling..."), and transient result messages. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	// ── Assembly page ───────────────────────────────────────────────────

	/** Assembly page's root — shown outside Draft and Experiment. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UWidget> AssemblyBody;

	/**
	 * One square icon box per required slot (after retail's ui_craft_assembly.inc
	 * "newcomp" slot frames), wrapped into a grid. An empty slot previews the
	 * type it wants — a live model of a matching bag item for Resource slots,
	 * a generic placeholder for Mixed/Identical ones (no single type to
	 * preview) — dimmed to mark it as a preview, not a placed item. Click an
	 * empty one to focus it; a filled one removes its first item.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> AssemblySlotsList;

	/** Bag contents offered for the focused slot — every item, since the server (not this list) validates fit. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> AssemblyCandidatesList;

	/** Sends the assemble command; enabled once every non-optional slot is full. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UButton> AssembleButton;

	// ── Experiment page ─────────────────────────────────────────────────
	// Built into the existing Content column at runtime so it shares the
	// window chrome with Draft and Assembly.
	UPROPERTY() TObjectPtr<UVerticalBox> ExperimentBody;
	UPROPERTY() TObjectPtr<UPanelWidget> ExperimentRowsList;
	UPROPERTY() TObjectPtr<UTextBlock> ExperimentPointsText;
	UPROPERTY() TObjectPtr<UButton> ExperimentButton;
	UPROPERTY() TObjectPtr<UButton> ExperimentContinueButton;

	// ── Customize page (Assembled/Customizing) ──────────────────────────
	// Also built at runtime, same as Experiment.
	UPROPERTY() TObjectPtr<UVerticalBox> CustomizeBody;
	UPROPERTY() TObjectPtr<class UEditableTextBox> CustomizeNameBox;
	UPROPERTY() TObjectPtr<UPanelWidget> CustomizeTemplateList;
	UPROPERTY() TObjectPtr<UPanelWidget> CustomizeVarsList;
	UPROPERTY() TObjectPtr<UButton> CustomizeApplyButton;
	UPROPERTY() TObjectPtr<UButton> CustomizeContinueButton;

	// ── Summary page (ReadyToFinish) ─────────────────────────────────────
	UPROPERTY() TObjectPtr<UVerticalBox> SummaryBody;
	UPROPERTY() TObjectPtr<UTextBlock> SummaryNameText;
	UPROPERTY() TObjectPtr<UButton> SummaryPracticeButton;
	UPROPERTY() TObjectPtr<UButton> SummaryCreateButton;
	UPROPERTY() TObjectPtr<UButton> SummarySchematicButton;
	UPROPERTY() TObjectPtr<UButton> SummaryRetrieveButton;

	// ── Prototype preview ────────────────────────────────────────────────
	// Persistent across Assembly/Experiment/Customize/Summary (not Draft,
	// which has no live prototype object yet) — a rotating live view of the
	// item under construction, via ModelWidget's SetObject(PrototypeId).
	UPROPERTY() TObjectPtr<UModelWidget> PrototypeModelView;

private:
	void RebuildTabs();
	void RebuildSchematicList();
	void RefreshDetails();
	void RefreshStatus();
	void RefreshPageVisibility();
	void RebuildAssemblySlots();
	void RebuildAssemblyCandidates();
	void RebuildExperimentRows();
	void RebuildCustomizePage();
	void RefreshSummaryPage();
	void RefreshPrototypeModel();
	void UpdateResourceTooltip(int64 ObjectId, UTextBlock* Tooltip);

	UFUNCTION() void HandleSessionStarted();
	UFUNCTION() void HandleStageChanged();
	UFUNCTION() void HandleSlotsChanged();
	UFUNCTION() void HandleSlotResult(ESWGCraftingSlotResult Result);
	UFUNCTION() void HandleAssemblyResult(ESWGCraftingResult Result);
	UFUNCTION() void HandleExperimentResult(ESWGCraftingResult Result);
	UFUNCTION() void HandleSessionClosed();
	UFUNCTION() void HandleDraftPreviewChanged();
	UFUNCTION() void HandleResourceExamineInfo(const FSWGExamineInfo& Info);
	UFUNCTION() void HandleSelectClicked();
	UFUNCTION() void HandleAssembleClicked();
	UFUNCTION() void HandleExperimentClicked();
	UFUNCTION() void HandleExperimentContinueClicked();
	UFUNCTION() void HandleCustomizeApplyClicked();
	UFUNCTION() void HandleCustomizeContinueClicked();
	UFUNCTION() void HandleSummaryPracticeClicked();
	UFUNCTION() void HandleSummaryCreateClicked();
	UFUNCTION() void HandleSummarySchematicClicked();
	UFUNCTION() void HandleSummaryRetrieveClicked();

	UPROPERTY()
	TObjectPtr<USWGCraftingSubsystem> Crafting;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;
	UPROPERTY() TObjectPtr<USWGExamineSubsystem> Examine;
	UPROPERTY() TObjectPtr<UTextBlock> HoveredResourceTooltip;
	int64 HoveredResourceId = 0;
	FString ResourceBaseDetails;
	TMap<int32, FString> SelectedResourceNames;
	TMap<int32, int64> SelectedResourceSourceIds;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> TabButtons;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> SchematicButtons;

	UPROPERTY()
	TArray<TObjectPtr<class USWGTravelPlanetClickForwarder>> SchematicForwarders;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> AssemblySlotButtons;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> AssemblyCandidateButtons;

	UPROPERTY()
	TArray<TObjectPtr<class USWGTravelPlanetClickForwarder>> ExperimentRowForwarders;

	UPROPERTY()
	TArray<TObjectPtr<class USWGTravelPlanetClickForwarder>> CustomizeForwarders;

	UPROPERTY()
	TArray<TObjectPtr<class USWGTravelPlanetClickForwarder>> ClickForwarders;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> ButtonTextTints;

	/** Distinct ToolTab bits present in the session, in first-seen order. */
	TArray<int32> VisibleTabs;
	/** 0 shows every tab's schematics; a specific bit filters to that tab. */
	int32 SelectedTab = 0;
	/** Index into Crafting->GetSchematics() of the browsed (not yet committed) schematic, or INDEX_NONE. */
	int32 HighlightedIndex = INDEX_NONE;
	/** Index into Crafting->GetSlots() the next candidate click fills, or INDEX_NONE. */
	int32 FocusedSlotIndex = INDEX_NONE;
	TArray<int32> ExperimentAllocations;
	bool bExperimentPending = false;
	/** Values to send for each customization variable next Apply, index-parallel with GetCustomizationVarNames(). */
	TArray<int32> CustomizeVarValues;
	/** Index into GetTemplateChoices(), or INDEX_NONE for "no change". */
	int32 SelectedTemplateChoice = INDEX_NONE;
	/** A slot/assembly result message, shown in StatusText until the next state or list change clears it. */
	FText TransientStatus;
	/** Category/subcategory keys currently collapsed in the Draft schematic tree ("weapon", "weapon/component", ...); empty by default (everything expanded), matching retail's top-level Expanded='true'. */
	TSet<FString> CollapsedSchematicNodes;
};
