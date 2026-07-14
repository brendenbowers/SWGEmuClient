#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * CmdStartSceneMessage (opcode 0x3AE6DFAE, opcount 0x09)
 *
 * Sent by the zone server to tell the client which scene to load and where
 * to spawn the player. Triggers the scene loading sequence.
 *
 * Wire layout:
 *   [0x09][0x3AE6DFAE]
 *   ignoreLayout(uint8) charID(int64) terrain(ascii)
 *   posX(float) posZ(float) posY(float)          ← NOTE: Z before Y on wire
 *   raceTemplate(ascii) galacticTime(int64)
 */
struct SWGEMU_API FCmdStartSceneMessage : public FSWGNetMessage
{
	uint8   IgnoreLayout  = 0;
	int64   CharacterID   = 0;
	FString TerrainName;
	float   PosX          = 0.f;
	float   PosZ          = 0.f;
	float   PosY          = 0.f;
	FString RaceTemplate;
	int64   GalacticTime  = 0;

	FCmdStartSceneMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
