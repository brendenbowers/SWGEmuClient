#include "Network/Messages/Zone/Object/MissionAccept.h"

FMissionAccept::FMissionAccept(uint64 PlayerId, uint64 InMissionObjectId, uint64 InTerminalObjectId, uint8 InTerminalIndex)
	: FObjectControllerMessage(0xF9u, PlayerId, 0x0Bu)
	, MissionObjectId(InMissionObjectId)
	, TerminalObjectId(InTerminalObjectId)
	, TerminalIndex(InTerminalIndex)
{}

FSWGPacket FMissionAccept::Serialize() const
{
	FSWGPacket Pkt = SerializeBase(0x10);

	Pkt.WriteUInt32(0); // "size" — parsed and ignored by the server
	Pkt.WriteUInt64(MissionObjectId);
	Pkt.WriteUInt64(TerminalObjectId);
	Pkt.WriteByte(TerminalIndex);

	return Pkt;
}
