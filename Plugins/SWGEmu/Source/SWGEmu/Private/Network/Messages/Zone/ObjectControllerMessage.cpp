


#include "Network/Messages/Zone/ObjectControllerMessage.h"
#include "Network/Messages/SWGMessageOp.h"

FObjectControllerMessage::FObjectControllerMessage(uint32 MessageType, uint64 ObjectId, uint32 MessagePriority)
	: MessageType(MessageType)
	, ObjectId(ObjectId)
	, MessagePriority(MessagePriority)
{
}

FSWGPacket FObjectControllerMessage::SerializeBase(uint16 OpcodeCount) const
{
	FSWGPacket Pkt;

	OpcodeCount++;
	uint32 Opcode = static_cast<uint32>(ESWGMessageOp::ObjControllerMessage);

	Pkt << OpcodeCount;
	Pkt << Opcode;
	
	uint32 Priority = MessagePriority;
	uint32 Type = MessageType;
	uint64 Id = ObjectId;

	Pkt << Priority;
	Pkt << Type;
	Pkt << Id;

	return Pkt;
}



