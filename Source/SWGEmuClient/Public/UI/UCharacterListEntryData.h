// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Flow/SWGFlowTypes.h"
#include "UCharacterListEntryData.generated.h"

/**
 * UObject wrapper around FSWGCharacterInfo so it can be used as a UListView item.
 * GalaxyName is resolved by the owning widget at population time (FSWGCharacterInfo
 * only carries GalaxyID).
 */
UCLASS(BlueprintType)
class SWGEMUCLIENT_API UCharacterListEntryData : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly)
	FSWGCharacterInfo Character;

	UPROPERTY(BlueprintReadOnly)
	FString GalaxyName;
};
