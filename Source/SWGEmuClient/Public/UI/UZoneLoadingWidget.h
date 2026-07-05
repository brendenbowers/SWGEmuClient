// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "CommonActivatableWidget.h"
#include "UZoneLoadingWidget.generated.h"

/**
 * Zone loading screen widget, shown on the LoadingLayer while the client waits
 * for the zone/terrain to finish loading after character select. Also reused
 * for ESWGClientState::Initialization (TRE/CRC-map loading at boot) — see
 * DT_FrontendFlowStateTransitions.
 *
 * Binds to BindWidget property: StatusText.
 * ProgressBar is optional (BindWidgetOptional) and hidden (Collapsed) by
 * default — callers that have a real 0..1 progress value can show it via
 * SetProgress(); callers that only have status text (like today's TRE
 * loading, which has no numeric progress source yet) can leave it alone.
 */
UCLASS()
class SWGEMUCLIENT_API UZoneLoadingWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/** Shows the progress bar (if bound) and sets its fill, 0..1. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void SetProgress(float InPercent);

	/** Hides the progress bar again (e.g. going back to an indeterminate/spinner-only state). */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|UI")
	void HideProgress();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleStatusUpdate(FText Status);

	UPROPERTY(meta = (BindWidget))
	UTextBlock* StatusText;

	UPROPERTY(meta = (BindWidgetOptional))
	UProgressBar* ProgressBar;
};
