#include "Network/Objects/Zone/Resource/ResourceContainerObjectBaseline.h"

namespace SWGResourceContainerBaselineParser
{
	void ParseBase3(FSWGPacket& Packet, FResourceContainerObjectBaseline& Out)
	{
		SWGTangibleBaselineParser::ParseBase3(Packet, Out.Tangible);

		Out.Quantity = Packet.ReadInt32();
		Out.SpawnId = Packet.ReadInt64();
		Out.bHasBase3 = true;
	}

	void ParseBase6(FSWGPacket& Packet, FResourceContainerObjectBaseline& Out)
	{
		Packet.ReadAsciiString(); // unused, server sends ""
		Packet.ReadInt32();
		Packet.ReadAsciiString(); // unused, server sends ""

		Out.ContainerName = Packet.ReadUnicodeString();
		Out.MaxStackSize = Packet.ReadInt32();
		Out.ResourceType = Packet.ReadAsciiString();
		Out.ResourceName = Packet.ReadUnicodeString();
		Out.bHasBase6 = true;
	}
}
