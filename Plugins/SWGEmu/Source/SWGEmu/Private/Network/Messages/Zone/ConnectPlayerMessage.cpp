#include "Network/Messages/Zone/ConnectPlayerMessage.h"
#include "Network/Messages/SWGMessageOp.h"

FSWGPacket FConnectPlayerMessage::Serialize() const
{
	FSWGPacket Pkt;

	uint16 OpcodeCount = 0x02;
	uint32 Opcode      = static_cast<uint32>(ESWGMessageOp::ConnectPlayerMessage);

	Pkt << OpcodeCount;
	Pkt << Opcode;

	return Pkt;
}
