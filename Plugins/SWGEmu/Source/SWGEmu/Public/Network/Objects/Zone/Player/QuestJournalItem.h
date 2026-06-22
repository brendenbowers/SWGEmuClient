#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FQuestJournalItem
{
	uint32 QuestCRC = 0;
	uint64 OwnerId = 0;
	int16 ActiveStepBitmask = 0;
	int16 CompletedStepBitmask = 0;
	uint8 CompletedFlag = 0;
	int32 QuestCounter = 0;

	bool Deserialize(FSWGPacket& Packet)
	{
		Packet << QuestCRC;
		Packet << OwnerId;
		Packet << ActiveStepBitmask;
		Packet << CompletedStepBitmask;
		Packet << CompletedFlag;
		Packet << QuestCounter;
		return true;
	}
};
