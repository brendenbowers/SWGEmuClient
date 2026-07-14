#include "Network/Messages/Zone/Object/TeleportAck.h"

FTeleportAck::FTeleportAck(uint64 ObjectId)
	: FObjectControllerMessage(0x13Fu, ObjectId)
{}

FSWGPacket FTeleportAck::Serialize() const
{
	FSWGPacket Pkt = SerializeBase(0x10);

	uint32 Move = MoveCount;
	Pkt << Move;

	return Pkt;
}
