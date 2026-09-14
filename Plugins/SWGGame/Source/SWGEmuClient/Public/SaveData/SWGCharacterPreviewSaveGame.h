#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "SWGCharacterPreviewSaveGame.generated.h"

USTRUCT()
struct FSWGCachedEquipment
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	uint32 TemplateCRC = 0;

	UPROPERTY(SaveGame)
	int32 ContainmentType = 0;

	UPROPERTY(SaveGame)
	TArray<uint8> CustomizationBytes;
};

USTRUCT()
struct FSWGCachedCharacterPreview
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	uint64 CharacterID = 0;

	UPROPERTY(SaveGame)
	uint32 BodyTemplateCRC = 0;

	UPROPERTY(SaveGame)
	FString CharacterName;

	UPROPERTY(SaveGame)
	TArray<uint8> CustomizationBytes;

	UPROPERTY(SaveGame)
	FString AlternateAppearance;

	UPROPERTY(SaveGame)
	TArray<FSWGCachedEquipment> Equipment;
};

UCLASS()
class SWGEMUCLIENT_API USWGCharacterPreviewSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	int32 CacheVersion = 1;

	UPROPERTY(SaveGame)
	TArray<FSWGCachedCharacterPreview> Characters;
};
