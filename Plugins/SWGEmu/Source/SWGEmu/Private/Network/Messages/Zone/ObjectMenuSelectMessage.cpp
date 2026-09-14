#include "Network/Messages/Zone/ObjectMenuSelectMessage.h"
#include "Network/Messages/SWGMessageOp.h"

FSWGPacket FObjectMenuSelectMessage::Serialize() const
{
	FSWGPacket Pkt;
	Pkt.WriteUInt16(0x02);
	Pkt.WriteUInt32(static_cast<uint32>(ESWGMessageOp::ObjectMenuSelect));
	Pkt.WriteUInt64(ObjectId);
	Pkt.WriteByte(RadialId);
	return Pkt;
}
