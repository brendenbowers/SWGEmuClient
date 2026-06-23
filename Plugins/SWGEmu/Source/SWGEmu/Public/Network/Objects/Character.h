#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGMessage.h"

struct FCharacter
{
	FString Name;
	uint32 RaceGenderCRC = 0;
	uint64 CharacterID = 0;
	uint32 ServerID = 0;
	uint32 Status = 0;

	bool Deserialize(FSWGMessage& Reader)
	{
		Name = Reader.ReadUnicodeString();   // server sends insertUnicode()
		Reader >> RaceGenderCRC >> CharacterID >> ServerID >> Status;
		return true;
	}
};
