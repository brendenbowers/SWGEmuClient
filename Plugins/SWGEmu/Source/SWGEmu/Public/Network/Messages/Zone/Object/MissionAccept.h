#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/Zone/ObjectControllerMessage.h"

/**
 * Accepts a mission out of the browser (sub-opcode 0xF9, MissionAcceptCallback
 * in Core3) — moves it from the mission_bag into the datapad server-side.
 * The server answers with a MissionAcceptResponse (sub-opcode 0xFA: missionId,
 * a success byte, TerminalIndex echoed back); the client doesn't need to wait
 * for it, since accepting also reparents the mission object, which arrives as
 * a normal UpdateContainmentMessage USWGMissionSubsystem already watches.
 *
 * Payload (MissionAcceptCallback::parse):
 *   size(int32, unused) missionObjectId(int64) terminalObjectId(int64) terminalIndex(byte)
 */
struct SWGEMU_API FMissionAccept : public FObjectControllerMessage
{
public:
	uint64 MissionObjectId  = 0;
	uint64 TerminalObjectId = 0;
	uint8  TerminalIndex    = 0;

	FMissionAccept(uint64 PlayerId, uint64 MissionObjectId, uint64 TerminalObjectId, uint8 TerminalIndex = 0);
	~FMissionAccept() = default;

	FSWGPacket Serialize() const;
};
