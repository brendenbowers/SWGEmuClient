#pragma once

#include "CoreMinimal.h"
#include "SWGFlowTypes.generated.h"

/** Blueprint-friendly mirror of FServerDetails + galaxy name from LoginEnumCluster. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGGalaxyInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32   GalaxyID   = 0;
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString IP;
	UPROPERTY(BlueprintReadOnly) int32   Port       = 0;
	UPROPERTY(BlueprintReadOnly) int32   Population = 0;
	UPROPERTY(BlueprintReadOnly) bool    bOnline    = false;
};

/** Blueprint-friendly mirror of FCharacter. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGCharacterInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int64   CharacterID    = 0;
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) int32   GalaxyID       = 0;
	UPROPERTY(BlueprintReadOnly) int32   RaceGenderCRC  = 0;
	UPROPERTY(BlueprintReadOnly) bool    bActive        = false;
};
