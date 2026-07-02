// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "UGalaxyListEntryWidget.generated.h"

/**
 * Single row in the galaxy list. Binds to BindWidget properties:
 * RowBackground, GalaxyNameText, PopulationText, StatusText.
 * Displays the FSWGGalaxyInfo carried by the bound UGalaxyListEntryData.
 * Selection is handled by the owning UGalaxySelectWidget via the list's
 * own selection (NextButton confirms whatever row is highlighted). RowBackground
 * is tinted to indicate the highlighted row.
 */
UCLASS()
class SWGEMUCLIENT_API UGalaxyListEntryWidget : public UUserWidget, public IUserObjectListEntry
{
	GENERATED_BODY()

protected:
	virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;
	virtual void NativeOnItemSelectionChanged(bool bIsSelected) override;

	UPROPERTY(meta = (BindWidget))
	UBorder* RowBackground;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* GalaxyNameText;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* PopulationText;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* StatusText;
};
