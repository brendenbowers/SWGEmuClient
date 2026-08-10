#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"

struct SWGEMU_API FEquiptmentItem
{
	// Binary customization payload (skin/color/pattern indices, etc.), same
	// escaped wire format as TANO base3's own "customization" field — see
	// FSWGCustomizationVariables. Raw bytes only, not decoded here: decoding
	// needs FSWGCustomizationVariables::Parse, which lives in the SWGEmuClient
	// module (this plugin can't depend on it) — see USWGEquipmentComponent::
	// BuildEquipmentVisuals for where it's actually decoded. Read via
	// ReadAsciiBytes, NOT ReadAsciiString, which would corrupt bytes 0x80-0x9F
	// by routing them through the system codepage (same reasoning as
	// USWGTangibleComponent::ApplyBase3Part1).
	TArray<uint8> CustomizationBytes;

	/** Raw wire value. See ESWGContainmentType / SWGIsSlottedArrangement / SWGGetArrangementGroupIndex. */
	int32 ContainmentType = 0;
	uint64 ObjectId = 0;
	uint32 TemplateCRC = 0;

	// Wire order (WearablesDeltaVector::insertItemToMessage):
	//   customizationString(ascii) containmentType(int32) objectId(int64) templateCrc(uint32)
	bool Deserialize(FSWGPacket& Packet)
	{
		CustomizationBytes = Packet.ReadAsciiBytes();
		ContainmentType = Packet.ReadInt32();
		ObjectId = Packet.ReadUInt64();
		TemplateCRC = Packet.ReadUInt32();
		return true;
	}
};
