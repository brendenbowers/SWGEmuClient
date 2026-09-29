#include "Network/Objects/Zone/ManufactureSchematic/ManufactureSchematicBaseline.h"

namespace
{
	/**
	 * One inner Vector<uint64>/Vector<int> nested inside a
	 * Uint64VectorDeltaVector/IntVectorDeltaVector baseline dump. Read as a
	 * plain count-prefixed array with no update counter of its own (only the
	 * outer DeltaVector carries one) — this inner shape is an inference, not
	 * source-verified; see the caveat on FManufactureSchematicBaseline::SlotOIDs.
	 */
	template<typename T, typename FReadItem>
	TArray<T> ReadNestedVector(FSWGPacket& Packet, FReadItem ReadItem)
	{
		TArray<T> Out;
		const int32 Count = Packet.ReadInt32();
		if (Packet.IsError() || Count < 0 || Count > 10000)
		{
			return Out;
		}
		Out.Reserve(Count);
		for (int32 Index = 0; Index < Count && !Packet.IsError(); ++Index)
		{
			Out.Add(ReadItem(Packet));
		}
		return Out;
	}
}

namespace SWGManufactureSchematicBaselineParser
{
	void ParseBase3(FSWGPacket& Packet, FManufactureSchematicBaseline& Out)
	{
		Out.Complexity = Packet.ReadFloat();
		Out.ObjectNameFile = Packet.ReadAsciiString();
		Packet.ReadInt32(); // filler
		Out.ObjectNameName = Packet.ReadAsciiString();
		Out.CustomName = Packet.ReadUnicodeString();
		Out.DataSize = Packet.ReadInt32(); // written via insertInt despite being a float server-side
		Out.ManufactureLimit = Packet.ReadInt32();
		Packet.ReadInt32(); // constant 1
		Packet.ReadInt32(); // constant 1
		Packet.ReadByte();  // constant 0
		Packet.ReadAsciiString(); // "crafting", constant
		Packet.ReadInt32(); // filler
		Packet.ReadAsciiString(); // "complexity", constant
		Packet.ReadFloat(); // complexity, repeated
		Out.PlayerName = Packet.ReadUnicodeString();
		Packet.ReadInt32(); // constant 25, purpose unknown
		Packet.ReadFloat(); // constant 8.0, purpose unknown
		Out.bHasBase3 = true;
	}

	void ParseBase6(FSWGPacket& Packet, FManufactureSchematicBaseline& Out)
	{
		Packet.ReadInt32(); // constant 0x76, "found in TANO6 packet" per Core3's own comment
		Packet.ReadInt32(); // constant 0
		Out.SchematicCrc = Packet.ReadUInt32();
		Out.bActiveCraft = Packet.ReadInt16() != 0;
		Out.bHasBase6 = true;
	}

	void ParseBase7(FSWGPacket& Packet, FManufactureSchematicBaseline& Out)
	{
		Out.IngredientNames = ReadBaselineVector<FSWGStringId>(Packet, [](FSWGPacket& P) { return FSWGStringId::Read(P); });
		Out.IngredientTypes = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });
		Out.SlotOIDs = ReadBaselineVector<TArray<uint64>>(Packet, [](FSWGPacket& P)
		{
			return ReadNestedVector<uint64>(P, [](FSWGPacket& Q) { return Q.ReadUInt64(); });
		});
		Out.SlotQuantities = ReadBaselineVector<TArray<int32>>(Packet, [](FSWGPacket& P)
		{
			return ReadNestedVector<int32>(P, [](FSWGPacket& Q) { return Q.ReadInt32(); });
		});
		Out.SlotQualities = ReadBaselineVector<float>(Packet, [](FSWGPacket& P) { return P.ReadFloat(); });
		Out.SlotClean = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });
		Out.SlotIndexes = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });
		Out.IngredientCounter = Packet.ReadByte();

		// Experimenting group titles.
		const int32 TitleCount = Packet.ReadInt32();
		Packet.ReadInt32(); // repeated count
		Out.ExperimentGroupTitles.Reset(TitleCount);
		for (int32 Index = 0; Index < TitleCount && !Packet.IsError(); ++Index)
		{
			Packet.ReadAsciiString(); // "crafting", constant
			Packet.ReadInt32(); // filler
			Out.ExperimentGroupTitles.Add(Packet.ReadAsciiString());
		}

		// Experimenting current percentages, index-parallel with the titles above.
		const int32 PercentCount = Packet.ReadInt32();
		Packet.ReadInt32(); // repeated count
		Out.ExperimentCurrentPercent.Reset(PercentCount);
		for (int32 Index = 0; Index < PercentCount && !Packet.IsError(); ++Index)
		{
			Out.ExperimentCurrentPercent.Add(Packet.ReadFloat());
		}

		// "Useless values", always 0 per Core3's own comment — kept for completeness.
		const int32 OffsetCount = Packet.ReadInt32();
		Packet.ReadInt32(); // repeated count
		Out.ExperimentOffsets.Reset(OffsetCount);
		for (int32 Index = 0; Index < OffsetCount && !Packet.IsError(); ++Index)
		{
			Out.ExperimentOffsets.Add(Packet.ReadInt32());
		}

		// Max experimentation value, always 1.0 per Core3's own comment.
		const int32 MaxCount = Packet.ReadInt32();
		Packet.ReadInt32(); // repeated count
		Out.ExperimentMax.Reset(MaxCount);
		for (int32 Index = 0; Index < MaxCount && !Packet.IsError(); ++Index)
		{
			Out.ExperimentMax.Add(Packet.ReadFloat());
		}

		// Five empty count-pair blocks, always (0,0) at this initial send — customization
		// name list, palette list, palette start index, palette end index, palette list
		// again (Core3 really does write "palette list" twice under separate comments;
		// verified byte-for-byte against ManufactureSchematicImplementation.cpp:231-256,
		// not just by its comment labels). Real values arrive later via MSCO7 delta
		// updates 0x0D-0x11, not here.
		for (int32 BlockIndex = 0; BlockIndex < 5 && !Packet.IsError(); ++BlockIndex)
		{
			Packet.ReadInt32();
			Packet.ReadInt32();
		}

		Out.CustomizationCounter = Packet.ReadByte();
		Out.RiskFactor = Packet.ReadFloat();
		Packet.ReadInt32(); // template list count, empty at initial send
		Packet.ReadInt32(); // repeated count
		Out.bReady = Packet.ReadByte() != 0;
		Out.bHasBase7 = true;
	}
}
