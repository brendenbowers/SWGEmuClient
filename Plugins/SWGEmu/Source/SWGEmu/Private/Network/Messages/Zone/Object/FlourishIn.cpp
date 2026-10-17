#include "Network/Messages/Zone/Object/FlourishIn.h"
#include "Network/SWGPacket.h"

bool FFlourishIn::Parse(FSWGPacket& Packet)
{
	FlourishId = Packet.ReadInt32();
	return !Packet.IsError();
}
