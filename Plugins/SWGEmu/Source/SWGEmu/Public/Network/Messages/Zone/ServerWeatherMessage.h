#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * ServerWeatherMessage (opcode 0x486356EA, opcount 0x03)
 *
 * The weather index (0 clear, 1 light storm, 2 storm, 3 heavy storm, 4 extreme storm: the row of the
 * planet's environment table) and the wind vector in SWG axes (x east, y up, z north). Core3 names the
 * floats windX, windMag, windY, but they are the vector's x, y, z in that order; the middle one is a
 * random value in [-1, 1], not a real updraft. Sent on scene ready and whenever the player's weather changes.
 *
 * Wire layout:
 *   [0x03][0x486356EA] weather(int32) windX(float) windUp(float) windZ(float)
 */
struct SWGEMU_API FServerWeatherMessage : public FSWGNetMessage
{
	int32 Weather = 0;
	float WindX = 0.f;
	float WindUp = 0.f;
	float WindZ = 0.f;

	FServerWeatherMessage(uint32 Opcode, FSWGMessage& Reader) : FSWGNetMessage(Opcode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
