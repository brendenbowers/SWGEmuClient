#include "Network/Objects/Zone/Installation/InstallationObjectBaseline.h"

namespace
{
	// The resource pool lists share a shape: count, an update counter that Core3
	// sets to the same count, then the entries.
	template<typename T, typename FReadItem>
	TArray<T> ReadResourceList(FSWGPacket& Packet, FReadItem ReadItem)
	{
		TArray<T> Out;
		const int32 Count = Packet.ReadInt32();
		Packet.ReadInt32(); // update counter
		Out.Reserve(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			Out.Add(ReadItem(Packet));
		}
		return Out;
	}
}

namespace SWGInstallationBaselineParser
{
	void ParseBase3(FSWGPacket& Packet, FInstallationObjectBaseline& Out)
	{
		SWGTangibleBaselineParser::ParseBase3(Packet, Out.Tangible);

		Out.ActiveFlag = Packet.ReadByte();
		Out.SurplusPower = Packet.ReadFloat();
		Out.BasePowerRate = Packet.ReadFloat();
		Out.bHasBase3 = true;
	}

	void ParseHarvesterBase3(FSWGPacket& Packet, FInstallationObjectBaseline& Out)
	{
		// Core3 hand-writes this one rather than deriving it, but the field order
		// matches the tangible layout right down to objectVisible.
		SWGTangibleBaselineParser::ParseBase3(Packet, Out.Tangible);

		Out.ActiveFlag = Packet.ReadByte();
		Packet.ReadInt32(); // unlabelled, hardcoded 0
		Out.bHasBase3 = true;
	}

	void ParseBase6(FSWGPacket& Packet, FInstallationObjectBaseline& Out)
	{
		SWGTangibleBaselineParser::ParseBase6(Packet, Out.Tangible);
		Out.bHasBase6 = true;
	}

	void ParseBase7(FSWGPacket& Packet, FInstallationObjectBaseline& Out)
	{
		Packet.ReadByte(); // resource pool update flag, always 1

		Out.ResourceIds = ReadResourceList<uint64>(Packet, [](FSWGPacket& P) { return P.ReadUInt64(); });
		// Sent twice, identically — the second copy is discarded.
		ReadResourceList<uint64>(Packet, [](FSWGPacket& P) { return P.ReadUInt64(); });

		Out.ResourceNames = ReadResourceList<FString>(Packet, [](FSWGPacket& P) { return P.ReadAsciiString(); });
		Out.ResourceTypes = ReadResourceList<FString>(Packet, [](FSWGPacket& P) { return P.ReadAsciiString(); });

		Out.ActiveResourceId = Packet.ReadInt64();
		Out.bOperating = Packet.ReadByte();
		Out.ExtractionRateDisplayed = Packet.ReadInt32();
		Out.ExtractionRateMax = Packet.ReadFloat();
		Out.CurrentExtractionRate = Packet.ReadFloat();
		Out.HopperSize = Packet.ReadFloat();
		Out.HopperSizeMax = Packet.ReadInt32();

		Out.HopperUpdateFlag = Packet.ReadByte();
		Out.Hopper = ReadBaselineVector<FHopperItem>(Packet, [](FSWGPacket& P)
		{
			FHopperItem Item;
			Item.Deserialize(P);
			return Item;
		});

		Out.ConditionPercent = Packet.ReadByte();
		Out.bHasBase7 = true;
	}
}
