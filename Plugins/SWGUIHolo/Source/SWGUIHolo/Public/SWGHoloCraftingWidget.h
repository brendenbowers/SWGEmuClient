#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGCameraTakeover.h"
#include "SWGHoloCraftingWidget.generated.h"

class ASWGHoloCraftingActor;
class UBorder;
class UButton;
class UEditableTextBox;
class UPanelWidget;
class UTextBlock;
class UVerticalBox;
class USWGCraftingSubsystem;
enum class ESWGCraftingSlotResult : uint8;
enum class ESWGCraftingResult : uint8;
enum class ESWGCraftingSessionState : uint8;

DECLARE_MULTICAST_DELEGATE(FSWGOnHoloCraftingEvent);

/** Projected crafting screens for Draft, Assembly, Experimentation, Customization, and final creation. */
UCLASS(Blueprintable)
class SWGUIHOLO_API USWGHoloCraftingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Close();
	void Dismiss();
	void SetSuspended(bool bInSuspended);
	FSWGOnHoloCraftingEvent OnClosed;
	FSWGOnHoloCraftingEvent OnSwitchToWindow;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void Project();
	void Refresh();
	void RefreshSelectionDetails();
	void OpenSection(int32 Index);
	void SelectNext(int32 Direction);
	void BackToSections();
	void Create();

	// ── Assembly (picking resources for each slot) ─────────────────────
	void OpenAssembly();
	void RefreshAssemblyPane();
	void RefreshAssemblyCard();
	void RefreshAssemblyCandidates();
	void PlaceStagePanels(const FGeometry& MyGeometry);
	void OpenLaterStage(ESWGCraftingSessionState State);
	void RebuildLaterStagePanel();
	UButton* AddStageAction(UPanelWidget* Parent, const FText& Caption, TFunction<void()> Action);
	void AdjustStageValue(int32 Direction);
	void ApplyExperiment();
	void ApplyCustomization();
	void SelectSlotNext(int32 Direction);
	void SelectCandidateNext(int32 Direction);
	void AddSelectedCandidateToFocusedSlot();
	void RemoveFocusedSlotItem();

	UFUNCTION() void HandleChanged();
	UFUNCTION() void HandlePreviewChanged();
	UFUNCTION() void HandleSlotsChanged();
	UFUNCTION() void HandleSlotResult(ESWGCraftingSlotResult Result);
	UFUNCTION() void HandleExperimentResult(ESWGCraftingResult Result);
	UFUNCTION() void HandleSessionClosed();
	UFUNCTION() void HandleCreateClicked();
	UFUNCTION() void HandleTabClicked();
	UFUNCTION() void HandleWindowClicked();
	UFUNCTION() void HandleCloseClicked();

	UPROPERTY() TObjectPtr<ASWGHoloCraftingActor> Hologram;
	UPROPERTY() TObjectPtr<USWGCraftingSubsystem> Crafting;
	UPROPERTY() TObjectPtr<UTextBlock> StatusText;
	UPROPERTY() TObjectPtr<UTextBlock> HintText;
	UPROPERTY() TObjectPtr<UVerticalBox> SectionList;
	UPROPERTY() TObjectPtr<UButton> CreateButton;
	UPROPERTY() TObjectPtr<UButton> TabButton;
	UPROPERTY() TObjectPtr<UButton> WindowButton;
	UPROPERTY() TObjectPtr<UButton> CloseButton;
	UPROPERTY() TArray<TObjectPtr<UObject>> ButtonTextTints;
	UPROPERTY() TArray<TObjectPtr<class USWGTravelPlanetClickForwarder>> SectionForwarders;
	UPROPERTY() TArray<TObjectPtr<UButton>> SectionButtons;

	/** The assembly component list, positioned beside the projected prototype each frame. */
	UPROPERTY() TObjectPtr<UBorder> AssemblyCardPanel;
	UPROPERTY() TObjectPtr<UTextBlock> AssemblyCardName;
	UPROPERTY() TObjectPtr<UVerticalBox> AssemblySlotList;
	UPROPERTY() TArray<TObjectPtr<class USWGTravelPlanetClickForwarder>> AssemblySlotForwarders;
	UPROPERTY() TArray<TObjectPtr<UBorder>> StagePanels;
	UPROPERTY() TArray<TObjectPtr<UVerticalBox>> StageBodies;
	UPROPERTY() TArray<TObjectPtr<class USWGTravelPlanetClickForwarder>> StageForwarders;
	UPROPERTY() TObjectPtr<UEditableTextBox> CustomizeNameBox;

	FSWGCameraTakeover View;
	TArray<int32> VisibleSchematicIndices;
	TArray<FString> Sections;
	FString ActiveSection;
	int32 SelectedSectionIndex = 0;
	bool bSuspended = false;

	/** Assembly state: set once OpenAssembly has built the floor rows and hidden the Draft carousel/section list. */
	bool bAssemblyOpened = false;
	int32 FocusedSlotIndex = 0;
	/** CandidateIdsBySlot[slotIndex][column] — mirrors what's on the floor for that slot, for AddIngredient. */
	TArray<TArray<int64>> CandidateIdsBySlot;
	int32 CurrentStageIndex = INDEX_NONE;
	int32 PreviousStageIndex = INDEX_NONE;
	FVector2D PreviousStageStartPosition = FVector2D::ZeroVector;
	float StageTransitionAlpha = 1.f;
	int32 FocusedStageRow = 0;
	TArray<int32> ExperimentAllocations;
	bool bExperimentPending = false;
	int32 SelectedTemplateChoice = INDEX_NONE;
	TArray<int32> CustomizeVarValues;
	FString CustomizeName;
};
