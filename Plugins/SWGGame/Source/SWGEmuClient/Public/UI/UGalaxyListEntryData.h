// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Flow/SWGFlowTypes.h"
#include "UGalaxyListEntryData.generated.h"

/**
 * UObject wrapper around FSWGGalaxyInfo so it can be used as a UListView item.
 */
UCLASS(BlueprintType)
class SWGEMUCLIENT_API UGalaxyListEntryData : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly)
	FSWGGalaxyInfo Galaxy;
};
