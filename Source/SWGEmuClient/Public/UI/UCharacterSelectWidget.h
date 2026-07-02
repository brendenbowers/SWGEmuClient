// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ListView.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "CommonActivatableWidget.h"
#include "UCharacterSelectWidget.generated.h"

/**
 * Character select screen widget.
 *
 * Lists the available characters (via CharacterListView) and lets the player pick one.
 * Binds to BindWidget properties: CharacterListView, StatusText, BackButton, NextButton,
 * CharacterNamePreviewText.
 *
 * Rows only display character data (see UCharacterListEntryWidget) - selecting a row
 * highlights it via the list's own selection, and NextButton confirms the highlighted
 * character by calling SelectCharacter on the flow subsystem.
 */
UCLASS()
class SWGEMUCLIENT_API UCharacterSelectWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

	/** Repopulates CharacterListView from the flow subsystem's current character list. */
	UFUNCTION(BlueprintCallable)
	void RefreshCharacterList();

	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnNextClicked();

	void OnCharacterSelectionChanged(UObject* Item);

	UPROPERTY(meta = (BindWidget))
	UListView* CharacterListView;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* StatusText;
	UPROPERTY(meta = (BindWidget))
	UButton* BackButton;
	UPROPERTY(meta = (BindWidget))
	UButton* NextButton;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* CharacterNamePreviewText;

private:
	UPROPERTY()
	TArray<UObject*> CharacterEntries;
};
