#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FSkillModifier
{
	FString SkillModString; // Map key (skill mod name)
	int32 BaseValue = 0;    // SkillModEntry::skillMod
	int32 Modifier = 0;     // SkillModEntry::skillBonus

	// Wire order (SkillModList entry, a DeltaVectorMap<String, SkillModEntry>):
	//   name(ascii) skillMod(int32) skillBonus(int32)
	// The leading map command byte is consumed by the caller (ReadBaselineMap).
	bool Deserialize(FSWGPacket& Packet)
	{
		SkillModString = Packet.ReadAsciiString();
		BaseValue = Packet.ReadInt32();
		Modifier = Packet.ReadInt32();
		return true;
	}
};
