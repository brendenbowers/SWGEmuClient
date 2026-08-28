#include "Network/Messages/Zone/Object/CommandQueueEnqueue.h"

FCommandQueueEnqueue::FCommandQueueEnqueue(uint64 ObjectId, uint32 ActionCRC, uint32 ActionCount, uint64 TargetId, const FString& Arguments)
	: FObjectControllerMessage(0x116u, ObjectId, 0x0Bu)
	, ActionCount(ActionCount)
	, ActionCRC(ActionCRC)
	, TargetId(TargetId)
	, Arguments(Arguments)
{}

FSWGPacket FCommandQueueEnqueue::Serialize() const
{
	FSWGPacket Pkt = SerializeBase(0x10);

	Pkt.WriteUInt32(0); // "size" — parsed and ignored by the server
	Pkt.WriteUInt32(ActionCount);
	Pkt.WriteUInt32(ActionCRC);
	Pkt.WriteUInt64(TargetId);
	Pkt.WriteUnicodeString(Arguments);

	return Pkt;
}
