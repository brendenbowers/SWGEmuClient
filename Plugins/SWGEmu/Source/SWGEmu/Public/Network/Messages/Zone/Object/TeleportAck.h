#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/Zone/ObjectControllerMessage.h"

/**
 * Acks a server-initiated teleport (sub-opcode 0x13F) — Core3 sets
 * PlayerObject::isTeleporting on every zone-in/teleport
 * (PlayerZoneComponent::switchZone/teleport) and DataTransformCallback::run
 * rejects every movement update with "!teleporting" until this is received
 * (TeleportAckCallback::run -> ghost->setTeleporting(false)). See
 * Network/Messages/Zone/Object/DataTransform.h for the sibling message this
 * mirrors.
 */
struct SWGEMU_API FTeleportAck : public FObjectControllerMessage
{
public:
	uint32 MoveCount;

	FTeleportAck(uint64 ObjectId);
	~FTeleportAck() = default;

	FSWGPacket Serialize() const;
};
