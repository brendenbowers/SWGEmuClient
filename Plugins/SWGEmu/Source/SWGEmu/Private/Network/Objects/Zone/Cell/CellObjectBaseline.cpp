#include "Network/Objects/Zone/Cell/CellObjectBaseline.h"

namespace SWGCellBaselineParser
{
	void ParseBase3(FSWGPacket& Packet, FCellObjectBaseline& Out)
	{
		Out.Complexity = Packet.ReadFloat();
		Out.ObjectName = FSWGStringId::Read(Packet);
		Out.CustomName = Packet.ReadUnicodeString();
		Out.Volume = Packet.ReadInt32();
		Out.CellNumber = Packet.ReadInt32();
		Out.bHasBase3 = true;
	}
}
