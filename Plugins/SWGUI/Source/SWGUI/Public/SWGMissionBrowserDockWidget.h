#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/SWGMissionSubsystem.h"
#include "SWGMissionBrowserDockWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;
class UModelWidget;

/**
 * The gamepad form of the mission browser (WBP_MissionBrowserDock): a panel
 * docked to the screen edge, like USWGInventoryDockWidget, instead of the
 * floating USWGMissionBrowserWidget window a mouse/keyboard player gets.
 * D-pad/stick walks the list, A accepts, Y refreshes, B closes.
 */
UCLASS(Abstract)
class SWGUI_API USWGMissionBrowserDockWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetMissions(const TArray<FSWGMissionEntry>& InMissions);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Mission")
	void Close();

	FSimpleMulticastDelegate OnClosed;

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent) override;

	/** The rows of the list. A scroll box, so the cursor can be kept in view. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UPanelWidget> ListPanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> ListEmptyText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailsTitleText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailsDescriptionText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailsRewardText;

	/** The cursor's mission's target (lair/camp/NPC) drawn live — see USWGItemIconSubsystem::RequestModelForTemplateCrc. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UModelWidget> DetailModel;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission")
	FLinearColor RowTextColor = FLinearColor::FromSRGBColor(FColor(0x96, 0xF4, 0xFC));

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Mission")
	FLinearColor CursorRowColor = FLinearColor(0.33f, 0.9f, 1.f, 0.35f);

private:
	void MoveCursor(int32 Delta);
	void RebuildList();
	void ApplyCursor();
	void Accept();
	void Refresh();

	TArray<FSWGMissionEntry> Missions;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> RowButtons;

	int32 Cursor = 0;
};
