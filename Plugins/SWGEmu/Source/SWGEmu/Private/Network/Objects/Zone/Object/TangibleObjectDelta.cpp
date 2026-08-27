#include "Network/Objects/Zone/Object/TangibleObjectDelta.h"

namespace SWGTangibleDeltaParser
{
	bool ApplyUpdate3(FSWGPacket& Packet, FTangibleObjectDelta& Out, uint16 Index)
	{
		switch (Index)
		{
			case 0x00: Out.Complexity = Packet.ReadFloat(); return true;
			case 0x01: Out.ObjectName = FSWGStringId::Read(Packet); return true;
			case 0x02: Out.CustomName = Packet.ReadUnicodeString(); return true;
			case 0x03: Out.Volume = Packet.ReadInt32(); return true;
			case 0x04: Out.CustomizationBytes = Packet.ReadAsciiBytes(); return true;
			case 0x05: Out.VisibleComponents = ReadInt32DeltaVectorChanges(Packet); return true;
			case 0x06: Out.OptionsBitmask = Packet.ReadInt32(); return true;
			case 0x07: Out.UseCount = Packet.ReadInt32(); return true;
			case 0x08: Out.ConditionDamage = Packet.ReadInt32(); return true;
			case 0x09: Out.MaxCondition = Packet.ReadInt32(); return true;
			case 0x0A: Out.ObjectVisible = Packet.ReadByte(); return true;
			default: return false;
		}
	}

	bool ApplyUpdate6(FSWGPacket& Packet, FTangibleObjectDelta& Out, uint16 Index)
	{
		switch (Index)
		{
			case 0x00: Out.Unknown076 = Packet.ReadInt32(); return true;
			case 0x01:
				Out.DefenderList = ReadDeltaVectorChanges<uint64>(Packet, [](FSWGPacket& P) { return P.ReadUInt64(); });
				return true;
			default: return false;
		}
	}

	void ParseDelta3(FSWGPacket& Packet, FTangibleObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			return ApplyUpdate3(P, Out, Index);
		});
	}

	void ParseDelta6(FSWGPacket& Packet, FTangibleObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			return ApplyUpdate6(P, Out, Index);
		});
	}
}
