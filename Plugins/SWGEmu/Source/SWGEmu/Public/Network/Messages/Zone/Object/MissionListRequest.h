#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/Zone/ObjectControllerMessage.h"

/**
 * Tells the server the player picked "Use" on a mission terminal (sub-opcode
 * 0xF5, MissionListRequestCallback in Core3).
 *
 * Mission terminals never answer ITEM_USE through ObjectMenuSelect —
 * MissionTerminalImplementation::handleObjectMenuSelect doesn't handle radial
 * id 20 at all, so that path is a dead end. Retail's client instead treats
 * "Use" on a mission terminal as a client-side special case (like Equip on a
 * wearable) and fires this message directly; MissionManager::
 * handleMissionListRequest is what actually refills and randomizes the
 * player's mission_bag in response.
 *
 * Payload (MissionListRequestCallback::parse):
 *   size(int32, unused server-side) flags(byte) seq(byte) terminalObjectId(int64)
 *
 * Seq is echoed back into each MissionObject's refreshCounter — just an
 * incrementing request id, not otherwise interpreted client-side.
 */
struct SWGEMU_API FMissionListRequest : public FObjectControllerMessage
{
public:
	uint8  Flags            = 0;
	uint8  Seq              = 0;
	uint64 TerminalObjectId = 0;

	FMissionListRequest(uint64 PlayerId, uint64 TerminalObjectId, uint8 Seq, uint8 Flags = 0);
	~FMissionListRequest() = default;

	FSWGPacket Serialize() const;
};
