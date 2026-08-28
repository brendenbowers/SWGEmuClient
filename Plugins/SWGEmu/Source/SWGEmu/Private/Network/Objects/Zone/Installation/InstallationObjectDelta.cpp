#include "Network/Objects/Zone/Installation/InstallationObjectDelta.h"

namespace SWGInstallationDeltaParser
{
	void ParseDelta3(FSWGPacket& Packet, FInstallationObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				// 0x0B is confirmed by InstallationObjectDeltaMessage3's operating
				// update; the two power fields follow it by declaration order.
				case 0x0B: Out.ActiveFlag = P.ReadByte(); return true;
				case 0x0C: Out.SurplusPower = P.ReadFloat(); return true;
				case 0x0D: Out.BasePowerRate = P.ReadFloat(); return true;
				default: return SWGTangibleDeltaParser::ApplyUpdate3(P, Out.Tangible, Index);
			}
		});
	}

	void ParseDelta6(FSWGPacket& Packet, FInstallationObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			return SWGTangibleDeltaParser::ApplyUpdate6(P, Out.Tangible, Index);
		});
	}

	void ParseDelta7(FSWGPacket& Packet, FInstallationObjectDelta& Out, uint16 UpdateCount)
	{
		// Indices 5, 6, 9, 0x0A, 0x0C and 0x0D are confirmed by
		// InstallationObjectDeltaMessage7's own updaters; the rest follow base7's
		// declaration order, which those six agree with.
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x00: Out.HopperUpdateFlag = P.ReadByte(); return true;
				case 0x01:
				case 0x02:
					Out.ResourceIds = ReadDeltaVectorChanges<uint64>(P, [](FSWGPacket& Q) { return Q.ReadUInt64(); });
					return true;
				case 0x03:
					Out.ResourceNames = ReadDeltaVectorChanges<FString>(P, [](FSWGPacket& Q) { return Q.ReadAsciiString(); });
					return true;
				case 0x04:
					Out.ResourceTypes = ReadDeltaVectorChanges<FString>(P, [](FSWGPacket& Q) { return Q.ReadAsciiString(); });
					return true;
				case 0x05: Out.ActiveResourceId = P.ReadInt64(); return true;
				case 0x06: Out.bOperating = P.ReadByte(); return true;
				case 0x07: Out.ExtractionRateDisplayed = P.ReadInt32(); return true;
				case 0x08: Out.ExtractionRateMax = P.ReadFloat(); return true;
				case 0x09: Out.CurrentExtractionRate = P.ReadFloat(); return true;
				case 0x0A: Out.HopperSize = P.ReadFloat(); return true;
				case 0x0B: Out.HopperSizeMax = P.ReadInt32(); return true;
				case 0x0C: Out.HopperUpdateFlag = P.ReadByte(); return true;
				case 0x0D:
					Out.Hopper = ReadDeltaVectorChanges<FHopperItem>(P, [](FSWGPacket& Q)
					{
						FHopperItem Item;
						Item.Deserialize(Q);
						return Item;
					});
					return true;
				case 0x0E: Out.ConditionPercent = P.ReadByte(); return true;
				default: return false;
			}
		});
	}
}
