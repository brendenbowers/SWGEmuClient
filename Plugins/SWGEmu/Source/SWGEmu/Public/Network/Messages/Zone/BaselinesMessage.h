#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * BaselinesMessage (opcode 0x68A75F0C, opcount 0x05)
 *
 * Sent by the zone server after SceneCreateObjectByCrc to push a full state
 * snapshot for one "slot" of an object (e.g. CREO base3/base4/base6/..., or
 * PLAY base8/base9). An object typically receives several of these — one per
 * slot — before a SceneEndBaselines confirms it's fully initialized.
 *
 * The per-slot field layout is object-type-specific (defined server-side by
 * dozens of different *ObjectMessageN classes in Core3) and is not decoded
 * here. RawPayload holds those opaque bytes so per-type decoders can be
 * layered on top later without changing the envelope parsing.
 *
 * Wire layout (matches Core3 BaseLineMessage):
 *   [0x05][0x68A75F0C]
 *   objectId(int64) objectType(uint32, FourCC e.g. 'CREO') baselineType(uint8, slot #)
 *   messageSize(uint32) operationCount(uint16)
 *   rawPayload(messageSize - 2 bytes)                 ← opaque per-type field data
 */
struct FBaselinesMessage : public FSWGNetMessage
{
	int64         ObjectId       = 0;
	uint32        ObjectType     = 0; // FourCC, e.g. CREO/PLAY/TANO — see ESWGMessageOp
	uint8         BaselineType   = 0; // Slot number (3, 4, 6, 7, 8, 9, ...)
	uint32        MessageSize    = 0; // Byte count of [operationCount + rawPayload]
	uint16        OperationCount = 0;
	TArray<uint8> RawPayload;

	FBaselinesMessage(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);

	/** Decode ObjectType back into its 4-character ASCII form (e.g. "CREO"). */
	FString GetObjectTypeFourCC() const;
};
