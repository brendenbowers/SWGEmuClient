#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FSkillModifier
{
	uint8 LeftoverDelta = 0;
	FString SkillModString;
	int32 BaseValue = 0;
	int32 Modifier = 0;

	bool Deserialize(FSWGPacket& Packet)
	{
		Packet << SkillModString;
		Packet << BaseValue;
		Packet << Modifier;
		return true;
	}
};
