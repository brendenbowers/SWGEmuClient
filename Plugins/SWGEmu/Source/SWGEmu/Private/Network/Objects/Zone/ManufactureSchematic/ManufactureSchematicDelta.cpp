#include "Network/Objects/Zone/ManufactureSchematic/ManufactureSchematicDelta.h"
#include "Network/Objects/Zone/Object/SWGDeltaListHelpers.h"

namespace
{
	/** Reads a base7-style list header (DeltaMessage::startList: count, updateCounter) and returns the count. */
	int32 ReadListCount(FSWGPacket& Packet)
	{
		const int32 Count = Packet.ReadInt32();
		Packet.ReadInt32(); // updateCounter, not tracked client-side (every send here is a full replacement)
		return Count;
	}
}

namespace SWGManufactureSchematicDeltaParser
{
	void ParseDelta3(FSWGPacket& Packet, FManufactureSchematicDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x00: Out.Complexity = P.ReadFloat(); return true;
				case 0x02: Out.Name = P.ReadUnicodeString(); return true;
				case 0x04: Out.Condition = P.ReadInt32(); return true;
				case 0x05:
				{
					const int32 Count = ReadListCount(P);
					TArray<FManufactureSchematicDelta::FCraftingValueUpdate> Values;
					Values.Reserve(Count);
					for (int32 Index2 = 0; Index2 < Count && !P.IsError(); ++Index2)
					{
						P.ReadByte(); // op, always 0 here (updateCraftingValues doesn't use the add/set op table)
						P.ReadAsciiString(); // "crafting", constant
						P.ReadInt32(); // filler
						FManufactureSchematicDelta::FCraftingValueUpdate& Value = Values.AddDefaulted_GetRef();
						Value.AttributeName = P.ReadAsciiString();
						Value.Value = P.ReadFloat();
					}
					Out.CraftingValues = MoveTemp(Values);
					return true;
				}
				default: return false;
			}
		});
	}

	void ParseDelta6(FSWGPacket& Packet, FManufactureSchematicDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x05: Out.HighlightSlot = P.ReadByte(); return true;
				default: return false;
			}
		});
	}

	void ParseDelta7(FSWGPacket& Packet, FManufactureSchematicDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				// 0x08 has a wider item: string-id file, filler, then title.
				case 0x08:
				{
					Out.GroupTitles = ReadDeltaVectorChanges<FString>(P, [](FSWGPacket& Q)
					{
						Q.ReadAsciiString(); // "crafting", constant
						Q.ReadInt32(); // filler
						return Q.ReadAsciiString();
					});
					return true;
				}
				case 0x09: Out.GroupCurrentPercent = ReadFloatDeltaVectorChanges(P); return true;
				case 0x0A: Out.GroupUnused0A = ReadFloatDeltaVectorChanges(P); return true; // never sent by any Core3 caller
				case 0x0B: Out.GroupLockValue = ReadFloatDeltaVectorChanges(P); return true;
				case 0x0C: Out.GroupMaxPercent = ReadFloatDeltaVectorChanges(P); return true;
				case 0x0D: Out.CustomizationVarNames = ReadAsciiStringDeltaVectorChanges(P); return true;
				case 0x0E: Out.CustomizationVarDefaults = ReadInt32DeltaVectorChanges(P); return true;
				case 0x0F: Out.CustomizationVarUnused0F = ReadInt32DeltaVectorChanges(P); return true;
				case 0x10: Out.CustomizationPaletteCounts = ReadInt32DeltaVectorChanges(P); return true;
				case 0x11: Out.CustomizationReady = P.ReadByte(); return true; // scalar, no list header
				case 0x12: Out.FailureRate = P.ReadFloat(); return true; // scalar, no list header
				case 0x13: Out.Templates = ReadAsciiStringDeltaVectorChanges(P); return true;
				case 0x14: Out.Unused14 = P.ReadByte(); return true; // scalar, no list header; never sent by any Core3 caller
				default: return false;
			}
		});
	}
}
