// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "UGalaxyListEntryWidget.generated.h"

/**
 * Single row in the galaxy list. Binds to BindWidget properties:
 * GalaxyNameText, PopulationText, StatusText, SelectButton.
 * Displays the FSWGGalaxyInfo carried by the bound UGalaxyListEntryData
 * and calls SelectGalaxy on the flow subsystem when clicked.
 */
UCLASS()
class SWGEMUCLIENT_API UGalaxyListEntryWidget : public UUserWidget, public IUserObjectListEntry
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;

	UFUNCTION()
	void OnSelectClicked();

	UPROPERTY(meta = (BindWidget))
	UTextBlock* GalaxyNameText;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* PopulationText;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* StatusText;
	UPROPERTY(meta = (BindWidget))
	UButton* SelectButton;

private:
	int32 GalaxyID = 0;
};
