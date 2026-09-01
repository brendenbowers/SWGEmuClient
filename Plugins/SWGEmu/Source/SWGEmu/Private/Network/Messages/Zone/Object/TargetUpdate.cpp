#include "Network/Messages/Zone/Object/TargetUpdate.h"

FTargetUpdate::FTargetUpdate(uint64 ObjectId, uint64 TargetId)
	: FObjectControllerMessage(0x126u, ObjectId, 0x0Bu)
	, TargetId(TargetId)
{}

FSWGPacket FTargetUpdate::Serialize() const
{
	FSWGPacket Pkt = SerializeBase(0x10);

	Pkt.WriteUInt32(0); // "size" — parsed and ignored by the server
	Pkt.WriteUInt64(TargetId);

	return Pkt;
}
