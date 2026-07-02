// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/TextBlock.h"
#include "CommonActivatableWidget.h"
#include "UZoneLoadingWidget.generated.h"

/**
 * Zone loading screen widget, shown on the LoadingLayer while the client waits
 * for the zone/terrain to finish loading after character select.
 * Binds to BindWidget property: StatusText.
 */
UCLASS()
class SWGEMUCLIENT_API UZoneLoadingWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleStatusUpdate(FText Status);

	UPROPERTY(meta = (BindWidget))
	UTextBlock* StatusText;
};
