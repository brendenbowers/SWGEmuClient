#include "Network/Objects/Zone/Resource/ResourceContainerObjectDelta.h"

namespace SWGResourceContainerDeltaParser
{
	void ParseDelta3(FSWGPacket& Packet, FResourceContainerObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x0B: Out.Quantity = P.ReadInt32(); return true;
				// Core3 only has this updater commented out, so the index is unconfirmed.
				case 0x0E: Out.SpawnId = P.ReadInt64(); return true;
				default: return SWGTangibleDeltaParser::ApplyUpdate3(P, Out.Tangible, Index);
			}
		});
	}

	void ParseDelta6(FSWGPacket& Packet, FResourceContainerObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x05: Out.ResourceType = P.ReadAsciiString(); return true;
				case 0x06: Out.ResourceName = P.ReadUnicodeString(); return true;
				default: return false;
			}
		});
	}
}
