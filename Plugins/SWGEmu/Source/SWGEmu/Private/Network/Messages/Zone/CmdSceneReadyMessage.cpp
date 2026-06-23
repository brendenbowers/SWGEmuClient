#include "Network/Messages/Zone/CmdSceneReadyMessage.h"
#include "Network/Messages/SWGMessageOp.h"

FSWGPacket FCmdSceneReadyMessage::Serialize() const
{
	FSWGPacket Pkt;

	uint16 OpcodeCount = 0x01;
	uint32 Opcode      = static_cast<uint32>(ESWGMessageOp::CmdSceneReady);

	Pkt << OpcodeCount;
	Pkt << Opcode;

	return Pkt;
}
