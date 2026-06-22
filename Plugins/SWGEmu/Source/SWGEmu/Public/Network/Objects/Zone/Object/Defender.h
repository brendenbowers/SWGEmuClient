#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

struct FDefender
{
	uint64 DefenderObjectId = 0;
	uint8 DefenderEndPosture = 0;
	uint8 HitType = 0;
	uint8 DefenderCombatSpecialMoveEffect = 0;

	bool Deserialize(FSWGPacket& Packet)
	{
		Packet << DefenderObjectId;
		Packet << DefenderEndPosture;
		Packet << HitType;
		Packet << DefenderCombatSpecialMoveEffect;
		return true;
	}
};
