#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Messages/SWGNetMessage.h"

/**
 * The sub-opcodes carried in an ObjectController envelope's Type field. Only
 * the ones we decode are listed; the server sends many more, and anything
 * unlisted is ignored rather than treated as an error.
 */
enum class ESWGObjControllerOp : uint32
{
	DataTransform      = 0x71u,  // Server-pushed teleport/bounce-back — needs a TeleportAck
	DataTransformWithParent = 0xF1u, // Same, for a player inside a cell — also needs the ack
	CombatAction       = 0xCCu,  // Who swung at whom, with what, and whether it landed
	CommandQueueRemove = 0x117u, // The reply to our CommandQueueEnqueue: cooldown + error
	CombatSpam         = 0x134u, // One line of the combat log
	ObjectMenuRequest  = 0x146u, // Client: what can I do with this object? (radial menu)
	ObjectMenuResponse = 0x147u, // The radial menu items the server offers for it
};

/**
 * Incoming ObjectController wrapper (opcode 0x80CE5E46) — the server side of
 * the same envelope Network/Messages/Zone/ObjectControllerMessage.h builds
 * for outgoing messages (FObjectControllerMessage::SerializeBase writes
 * Priority, Type, ObjectId in that order; this mirrors it for reading).
 *
 * The zero int Core3 writes after ObjectId is the movement tick counter, only
 * meaningful on DataTransform; it is consumed as part of the envelope so
 * RawPayload starts at the sub-message's first real field. Zone/Object/*In.h
 * decodes those, since which one applies depends on Type.
 */
struct SWGEMU_API FObjControllerMessageIn : public FSWGNetMessage
{
	uint32 Priority = 0;
	uint32 Type = 0;
	int64 ObjectId = 0;

	/** The movement tick counter. Only DataTransform gives it a meaning. */
	uint32 TickCount = 0;

	/** Everything after the envelope — the sub-message's own fields. */
	TArray<uint8> RawPayload;

	FObjControllerMessageIn(uint32 OPCode, FSWGMessage& Reader) : FSWGNetMessage(OPCode, Reader) { Deserialize(Reader); }

	bool Deserialize(FSWGMessage& Reader);

	/** Type as an ESWGObjControllerOp, for switching on. Unknown types keep their raw value. */
	ESWGObjControllerOp GetSubOp() const { return static_cast<ESWGObjControllerOp>(Type); }

	FSWGPacket AsPayloadPacket() const { return FSWGPacket(RawPayload.GetData(), RawPayload.Num()); }
};
