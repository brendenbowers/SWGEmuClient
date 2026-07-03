#include "Network/Objects/Zone/Object/TangibleObjectBaseline.h"

namespace SWGTangibleBaselineParser
{
	void ParseBase3(FSWGPacket& Packet, FTangibleObjectBaseline& Out)
	{
		Out.Complexity = Packet.ReadFloat();
		Out.ObjectName = FSWGStringId::Read(Packet);
		Out.CustomName = Packet.ReadUnicodeString();
		Out.Volume = Packet.ReadInt32();
		Out.CustomizationString = Packet.ReadAsciiString();

		Out.VisibleComponents = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });

		Out.OptionsBitmask = Packet.ReadInt32();
		Out.UseCount = Packet.ReadInt32();
		Out.ConditionDamage = Packet.ReadInt32();
		Out.MaxCondition = Packet.ReadInt32();
		Out.ObjectVisible = Packet.ReadByte();
		Out.bHasBase3 = true;
	}

	void ParseBase6(FSWGPacket& Packet, FTangibleObjectBaseline& Out)
	{
		Out.Unknown076 = Packet.ReadInt32();
		Out.DefenderList = ReadBaselineVector<uint64>(Packet, [](FSWGPacket& P) { return P.ReadUInt64(); });
		Out.bHasBase6 = true;
	}
}
