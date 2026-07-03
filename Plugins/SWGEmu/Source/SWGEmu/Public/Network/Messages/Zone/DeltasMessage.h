#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * DeltasMessage (opcode 0x12862153, opcount 0x05)
 *
 * Sent by the zone server whenever a field on an already-created object
 * changes (position aside — that goes over UpdateTransformMessage). Same
 * envelope shape as BaselinesMessage, but the payload is a sequence of
 * [fieldIndex(uint16)][value] update operations rather than a full snapshot.
 *
 * The value type for each update depends on the object type + field index
 * (defined server-side by dozens of different *ObjectDeltaMessageN classes
 * in Core3) and is not decoded here. RawUpdates holds those opaque bytes so
 * per-type decoders can be layered on top later without changing the
 * envelope parsing.
 *
 * Wire layout (matches Core3 DeltaMessage):
 *   [0x05][0x12862153]
 *   objectId(int64) objectType(uint32, FourCC e.g. 'CREO') deltaType(uint8, slot #)
 *   messageSize(uint32) updateCount(uint16)
 *   rawUpdates(messageSize - 2 bytes)                 ← opaque [fieldIndex][value]...
 */
struct FDeltasMessage : public FSWGNetMessage
{
	int64         ObjectId    = 0;
	uint32        ObjectType  = 0; // FourCC, e.g. CREO/PLAY/TANO — see ESWGMessageOp
	uint8         DeltaType   = 0; // Slot number (3, 4, 6, 7, 8, 9, ...)
	uint32        MessageSize = 0; // Byte count of [updateCount + rawUpdates]
	uint16        UpdateCount = 0;
	TArray<uint8> RawUpdates;

	FDeltasMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);

	/** Decode ObjectType back into its 4-character ASCII form (e.g. "CREO"). */
	FString GetObjectTypeFourCC() const;
};
