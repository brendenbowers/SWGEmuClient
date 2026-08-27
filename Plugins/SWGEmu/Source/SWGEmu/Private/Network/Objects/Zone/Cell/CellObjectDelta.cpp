#include "Network/Objects/Zone/Cell/CellObjectDelta.h"

namespace SWGCellDeltaParser
{
	void ParseDelta3(FSWGPacket& Packet, FCellObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x05: Out.CellNumber = P.ReadInt32(); return true;
				default: return false;
			}
		});
	}
}
