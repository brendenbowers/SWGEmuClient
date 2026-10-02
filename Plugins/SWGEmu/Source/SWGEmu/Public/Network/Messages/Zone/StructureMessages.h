#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

struct SWGEMU_API FEnterStructurePlacementModeMessage : public FSWGNetMessage
{
	uint64 DeedId = 0;
	FString ClientTemplatePath;

	FEnterStructurePlacementModeMessage(uint32 Opcode, FSWGMessage& Reader) : FSWGNetMessage(Opcode, Reader) { Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
};
