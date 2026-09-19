#include "Network/Messages/Zone/Object/MissionListRequest.h"

FMissionListRequest::FMissionListRequest(uint64 PlayerId, uint64 InTerminalObjectId, uint8 InSeq, uint8 InFlags)
	: FObjectControllerMessage(0xF5u, PlayerId, 0x0Bu)
	, Flags(InFlags)
	, Seq(InSeq)
	, TerminalObjectId(InTerminalObjectId)
{}

FSWGPacket FMissionListRequest::Serialize() const
{
	FSWGPacket Pkt = SerializeBase(0x10);

	Pkt.WriteUInt32(0); // "size" — parsed and ignored by the server
	Pkt.WriteByte(Flags);
	Pkt.WriteByte(Seq);
	Pkt.WriteUInt64(TerminalObjectId);

	return Pkt;
}
