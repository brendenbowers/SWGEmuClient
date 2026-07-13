#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * Incoming ObjectController wrapper (opcode 0x80CE5E46) — the server side of
 * the same envelope Network/Messages/Zone/ObjectControllerMessage.h builds
 * for outgoing messages (FObjectControllerMessage::SerializeBase writes
 * Priority, Type, ObjectId in that order; this mirrors it for reading).
 *
 * Only used today to detect Core3 re-arming PlayerObject::isTeleporting via
 * a server-pushed DataTransform (Type 0x71) — e.g. DataTransformCallback's
 * speed-hack/invalid-position bounce back calls SceneObject::teleport(),
 * which re-sets isTeleporting and pushes this message, not
 * UpdateTransformMessage/LightUpdateTransformMessage's regular sync
 * broadcast. Sub-message payload past ObjectId is intentionally not parsed —
 * see USWGObjectGraphSubsystem's handler for why an unconditional ack is
 * safe regardless of Type.
 */
struct FObjControllerMessageIn : public FSWGNetMessage
{
	uint32 Priority = 0;
	uint32 Type = 0;
	int64 ObjectId = 0;

	FObjControllerMessageIn(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);
};
