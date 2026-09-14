// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ListView.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "CommonActivatableWidget.h"
#include "UGalaxySelectWidget.generated.h"

/**
 * Galaxy select screen widget.
 *
 * Lists the available galaxies (via GalaxyListView) and lets the player pick one.
 * Binds to BindWidget properties: GalaxyListView, StatusText, BackButton, NextButton.
 *
 * Rows only display galaxy data (see UGalaxyListEntryWidget) - selecting a row
 * highlights it via the list's own selection, and NextButton confirms the highlighted
 * galaxy by calling SelectGalaxy on the flow subsystem.
 */
UCLASS()
class SWGEMUCLIENT_API UGalaxySelectWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

	/** Repopulates GalaxyListView from the flow subsystem's current galaxy list. */
	UFUNCTION(BlueprintCallable)
	void RefreshGalaxyList();

	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnNextClicked();

	UPROPERTY(meta = (BindWidget))
	UListView* GalaxyListView;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* StatusText;
	UPROPERTY(meta = (BindWidget))
	UButton* BackButton;
	UPROPERTY(meta = (BindWidget))
	UButton* NextButton;

private:
	UPROPERTY()
	TArray<UObject*> GalaxyEntries;
};
