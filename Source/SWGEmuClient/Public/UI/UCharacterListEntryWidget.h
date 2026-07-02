// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "UCharacterListEntryWidget.generated.h"

/**
 * Single row in the character list. Binds to BindWidget properties:
 * CharacterNameText, GalaxyText, StatusText, SelectButton.
 * Displays the FSWGCharacterInfo carried by the bound UCharacterListEntryData
 * and calls SelectCharacter on the flow subsystem when clicked.
 */
UCLASS()
class SWGEMUCLIENT_API UCharacterListEntryWidget : public UUserWidget, public IUserObjectListEntry
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;

	UFUNCTION()
	void OnSelectClicked();

	UPROPERTY(meta = (BindWidget))
	UTextBlock* CharacterNameText;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* GalaxyText;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* StatusText;
	UPROPERTY(meta = (BindWidget))
	UButton* SelectButton;

private:
	int64 CharacterID = 0;
};
