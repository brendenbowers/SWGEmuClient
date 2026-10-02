#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SWGCameraTakeover.h"
#include "Subsystems/SWGSurveySubsystem.h"
#include "SWGHoloSurveyWidget.generated.h"

class ASWGHoloSurveyActor;
class ASWGHoloSurveyFieldActor;
class UButton;
class UTextBlock;
class USWGSurveySubsystem;
class USWGTreSubsystem;

DECLARE_MULTICAST_DELEGATE(FSWGOnHoloSurveyEvent);

/**
 * The survey tool as a hologram. A droid projects the resources the tool can
 * find in front of the player; pick one and survey, and the
 * concentrations are projected onto the real ground around the player at full
 * scale (ASWGHoloSurveyFieldActor) while the player stays beside the droid.
 * Up/down or the wheel picks, Enter / click / A surveys, Q / X samples,
 * Tab / Y opens the window instead.
 *
 * Builds its own hint bar when used from C++; a Blueprint subclass may lay out
 * its own, binding the optional widgets below.
 */
UCLASS(Blueprintable)
class SWGUIHOLO_API USWGHoloSurveyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloSurvey")
	void Close();

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloSurvey")
	void SwitchToWindow();

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloSurvey")
	void SelectNext(int32 Direction);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloSurvey")
	void Survey();

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|HoloSurvey")
	void Sample();

	/** Using the tool again brings its projected picker back in front of the player. */
	void ShowPicker();

	/** Steps aside while the holo map has the camera, keeping any scan in the world; false comes back as it was. */
	void SetSuspended(bool bInSuspended);

	FSWGOnHoloSurveyEvent OnClosed;
	FSWGOnHoloSurveyEvent OnSwitchToWindow;

	/** World units in front of the player the projector stands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloSurvey")
	float ProjectorDistance = 130.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloSurvey")
	float ProjectorHeight = 95.f;

	/** Height of the droid over the survey field in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloSurvey")
	float ScanDroidHeight = 750.f;

	/** Keeps the raised droid visible ahead of the player while surveying. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloSurvey")
	float ScanDroidForwardDistance = 2300.f;

	/** Over-the-shoulder camera, relative to the projector in the player's frame: back, right, up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloSurvey")
	FVector CameraOffset = FVector(-300.f, 130.f, 135.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloSurvey")
	float CameraBlendSeconds = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloSurvey", meta = (ClampMin = "0", ClampMax = "1"))
	float HudOpacity = 0.12f;

	/** Time for the surveyed area and projector rays to collapse on close. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SWGEmu|HoloSurvey")
	float CollapseSeconds = 1.2f;

	/** Gives a Blueprint's bound texts the holo font and colour; off leaves them as designed. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|HoloSurvey")
	bool bApplyHoloStyle = true;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** Optional: what the tool is doing and how the last survey went. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SurveyButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SampleButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> WindowButton;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

private:
	void Project();
	void RestoreView();
	void RefreshEntries();
	void BuildPickerEntries();
	void BackPicker();
	void RefreshStatus();
	void ShowField();
	/** Picks the list row under a viewport position; INDEX_NONE if none is near. */
	int32 EntryAt(const FVector2D& ViewportPosition) const;
	void UpdateDroidRays();
	TArray<FVector> GetFieldEdgeTargets() const;

	UFUNCTION() void HandleResourcesChanged();
	UFUNCTION() void HandleSurveyStateChanged();
	UFUNCTION() void HandleResultReceived();
	UFUNCTION() void HandleSurveyClicked();
	UFUNCTION() void HandleSampleClicked();
	UFUNCTION() void HandleWindowClicked();
	UFUNCTION() void HandleCloseClicked();

	UPROPERTY()
	TObjectPtr<ASWGHoloSurveyActor> Hologram;

	UPROPERTY()
	TObjectPtr<ASWGHoloSurveyFieldActor> Field;

	UPROPERTY()
	TObjectPtr<USWGSurveySubsystem> SurveySubsystem;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> ButtonTextTints;

	FSWGCameraTakeover View;
	/** Resource names or empty for category rows, parallel to the hologram's entries. */
	TArray<FString> EntryNames;
	TArray<FString> EntryBranches;
	TArray<FSWGSurveyResource> PickerResources;
	TArray<FString> PickerPath;
	TArray<int32> PickerHistory;
	bool bExploring = false;
	bool bSuspended = false;
	FText TransientStatus;
};
