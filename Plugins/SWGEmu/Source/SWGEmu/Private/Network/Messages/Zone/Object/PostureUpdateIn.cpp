#include "Network/Messages/Zone/Object/PostureUpdateIn.h"
#include "Network/SWGPacket.h"

bool FPostureUpdateIn::Parse(FSWGPacket& Packet)
{
	Posture   = Packet.ReadByte();
	Immediate = Packet.ReadByte();

	return !Packet.IsError();
}
