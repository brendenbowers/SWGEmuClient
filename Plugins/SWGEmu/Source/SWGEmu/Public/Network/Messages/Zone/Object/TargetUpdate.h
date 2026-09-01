#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/Zone/ObjectControllerMessage.h"

/**
 * Tells the server what the player currently has targeted (sub-opcode 0x126).
 *
 * The server stores this on the CreatureObject and echoes it back as CREO
 * delta 6 index 0x09, so the authoritative target always arrives through the
 * normal baseline/delta path — this message only pushes our intent up.
 * TargetId 0 clears the target.
 *
 * Payload (TargetUpdateCallback::parse):
 *   size(int32, unused server-side) targetId(int64)
 */
struct SWGEMU_API FTargetUpdate : public FObjectControllerMessage
{
public:
	uint64 TargetId = 0;

	FTargetUpdate(uint64 ObjectId, uint64 TargetId = 0);
	~FTargetUpdate() = default;

	FSWGPacket Serialize() const;
};
