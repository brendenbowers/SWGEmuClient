// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "UCharacterListEntryWidget.generated.h"

/**
 * Single row in the character list. Binds to BindWidget properties:
 * RowBackground, CharacterNameText, GalaxyText, StatusText.
 * Displays the FSWGCharacterInfo carried by the bound UCharacterListEntryData.
 * Selection is handled by the owning UCharacterSelectWidget via the list's
 * own selection (NextButton confirms whatever row is highlighted). RowBackground
 * is tinted to indicate the highlighted row.
 */
UCLASS()
class SWGEMUCLIENT_API UCharacterListEntryWidget : public UUserWidget, public IUserObjectListEntry
{
	GENERATED_BODY()

protected:
	virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;
	virtual void NativeOnItemSelectionChanged(bool bIsSelected) override;

	UPROPERTY(meta = (BindWidget))
	UBorder* RowBackground;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* CharacterNameText;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* GalaxyText;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* StatusText;
};
