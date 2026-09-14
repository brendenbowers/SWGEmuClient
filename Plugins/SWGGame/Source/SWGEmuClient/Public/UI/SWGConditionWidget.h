#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "SWGConditionWidget.generated.h"

class USWGHealthComponent;

/**
 * The player's HAM pools. SWG tracks nine attributes but only three are pools
 * with a current/max pair the UI shows — the other six are their secondary
 * attributes.
 */
UENUM(BlueprintType)
enum class ESWGHamPool : uint8
{
	Health      = 0,
	Strength    = 1,
	Constitution= 2,
	Action      = 3,
	Quickness   = 4,
	Stamina     = 5,
	Mind        = 6,
	Focus       = 7,
	Willpower   = 8,
};

/**
 * Health/Action/Mind readout for the local player.
 *
 * The health component has no change delegate, so this polls on a timer rather
 * than every frame — HAM deltas arrive far slower than the frame rate, and a
 * poll avoids every component needing to grow a broadcast for the first bit of
 * UI that reads it.
 */
UCLASS(Abstract)
class SWGEMUCLIENT_API USWGConditionWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Condition")
	int32 Health = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Condition")
	int32 MaxHealth = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Condition")
	int32 Action = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Condition")
	int32 MaxAction = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Condition")
	int32 Mind = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Condition")
	int32 MaxMind = 0;

	/** 0..1 fill for a pool, 0 when the max isn't known yet. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Condition")
	float GetPoolFraction(ESWGHamPool Pool) const;

	/** Fired after a refresh that actually changed something — drive the visuals from here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SWGEmu|Condition")
	void OnConditionUpdated();

	/** Re-reads the player's health component. Called on the poll timer, safe to call directly. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Condition")
	void RefreshCondition();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** How often the pools are re-read, in seconds. */
	UPROPERTY(EditDefaultsOnly, Category = "SWGEmu|Condition")
	float RefreshInterval = 0.25f;

	// Filled directly on refresh when present, so a bar-only panel needs no
	// Blueprint graph. OnConditionUpdated still fires for anything richer.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ActionBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> MindBar;

	// "current / max" per pool, the way SWG labels its vitals.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ActionText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MindText;

private:
	USWGHealthComponent* ResolveHealthComponent() const;

	FTimerHandle RefreshTimer;
};
