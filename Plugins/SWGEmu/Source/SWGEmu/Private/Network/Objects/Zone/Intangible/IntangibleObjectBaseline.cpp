#include "Network/Objects/Zone/Intangible/IntangibleObjectBaseline.h"

namespace SWGIntangibleBaselineParser
{
	void ParseBase3(FSWGPacket& Packet, FIntangibleObjectBaseline& Out)
	{
		Out.UnknownVersion = Packet.ReadFloat();
		Out.ObjectName = FSWGStringId::Read(Packet);
		Out.CustomName = Packet.ReadUnicodeString();
		Out.Volume = Packet.ReadInt32();

		Out.bHasBase3 = true;
	}
}
