#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct SWGEMU_API FQuestJournalItem
{
	uint32 QuestCRC = 0;
	uint64 OwnerId = 0;
	int16 ActiveStepBitmask = 0;
	int16 CompletedStepBitmask = 0;
	uint8 CompletedFlag = 0;
	int32 QuestCounter = 0;

	// Wire order (PlayerObject quests entry, a DeltaVectorMap<uint32, PlayerQuestData>):
	//   questCrc(uint32, map key) ownerId(int64) activeStepBitmask(uint16)
	//   completedStepBitmask(uint16) completedFlag(uint8) questCounter(int32)
	// Leading map command byte consumed by the caller.
	bool Deserialize(FSWGPacket& Packet)
	{
		QuestCRC = Packet.ReadUInt32();
		OwnerId = Packet.ReadUInt64();
		ActiveStepBitmask = Packet.ReadInt16();
		CompletedStepBitmask = Packet.ReadInt16();
		CompletedFlag = Packet.ReadByte();
		QuestCounter = Packet.ReadInt32();
		return true;
	}
};
