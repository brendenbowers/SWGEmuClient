#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "UI/SWGConditionWidget.h"
#include "SWGTargetBoxWidget.generated.h"

class USWGTargetSubsystem;

/**
 * The target window: who the player has targeted and how hurt they are —
 * SWG's target box, which shows the target's name, level and its three HAM
 * bars in the same layout the player's own condition readout uses.
 */
UCLASS(Abstract)
class SWGEMUCLIENT_API USWGTargetBoxWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Target")
	int64 TargetId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Target")
	FText TargetName;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Target")
	int32 TargetLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Target")
	int32 Health = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Target")
	int32 MaxHealth = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Target")
	int32 Action = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Target")
	int32 MaxAction = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Target")
	int32 Mind = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Target")
	int32 MaxMind = 0;

	/** 0..1 fill for a pool, 0 when the max isn't known yet. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Target")
	float GetPoolFraction(ESWGHamPool Pool) const;

	/**
	 * SWG's con colour for the target's level relative to the player's:
	 * green (easy) through red (much higher). White when either level is
	 * unknown.
	 */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Target")
	FLinearColor GetDifficultyColor() const;

	/** Fired after a refresh that actually changed something — drive the visuals from here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SWGEmu|Target")
	void OnTargetUpdated();

	/** Fired when the target changes identity, including to "none". */
	UFUNCTION(BlueprintImplementableEvent, Category = "SWGEmu|Target")
	void OnTargetChanged(int64 NewTargetId);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Target")
	void RefreshTarget();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Target")
	float RefreshInterval = 0.25f;

	// Filled directly on refresh when present, so a bar-only panel needs no
	// Blueprint graph — same arrangement as USWGConditionWidget.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ActionBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> MindBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LevelText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ActionText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MindText;

private:
	UFUNCTION()
	void HandleTargetChanged(int64 NewTargetId, AActor* NewTargetActor);

	USWGTargetSubsystem* GetTargetSubsystem() const;

	/** Re-reads the target's identity fields (name, level) — only needed on a target switch. */
	void RefreshIdentity();

	/** Zeroes every field and hides the widget. */
	void ClearDisplay();

	FTimerHandle RefreshTimer;
};
