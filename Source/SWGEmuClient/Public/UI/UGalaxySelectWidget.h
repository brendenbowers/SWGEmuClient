// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ListView.h"
#include "Components/TextBlock.h"
#include "CommonActivatableWidget.h"
#include "UGalaxySelectWidget.generated.h"

/**
 * Galaxy select screen widget.
 *
 * Lists the available galaxies (via GalaxyListView) and lets the player pick one.
 * Binds to BindWidget properties: GalaxyListView, StatusText.
 * Each row is a UGalaxyListEntryWidget that calls SelectGalaxy on the flow
 * subsystem directly when clicked.
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

	UPROPERTY(meta = (BindWidget))
	UListView* GalaxyListView;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* StatusText;

private:
	UPROPERTY()
	TArray<UObject*> GalaxyEntries;
};
