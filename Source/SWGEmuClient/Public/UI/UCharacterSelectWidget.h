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
 * Binds to BindWidget properties: CharacterListView, StatusText, ExitButton.
 * Each row is a UCharacterListEntryWidget that calls SelectCharacter on the flow
 * subsystem directly when clicked.
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
	void OnExitClicked();

	UPROPERTY(meta = (BindWidget))
	UListView* CharacterListView;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* StatusText;
	UPROPERTY(meta = (BindWidget))
	UButton* ExitButton;

private:
	UPROPERTY()
	TArray<UObject*> CharacterEntries;
};
