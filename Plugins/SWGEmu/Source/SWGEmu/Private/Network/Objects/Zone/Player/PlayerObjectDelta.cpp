#include "Network/Objects/Zone/Player/PlayerObjectDelta.h"

namespace
{
	// PlayerObjectDeltaMessage3's bitmask updates aren't a list-change run —
	// they're a plain count followed by that many ints.
	TArray<uint32> ReadBitmaskArray(FSWGPacket& Packet)
	{
		TArray<uint32> Out;
		const int32 Count = Packet.ReadInt32();
		Out.Reserve(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			Out.Add(Packet.ReadUInt32());
		}
		return Out;
	}

	template<typename T>
	TSWGListChanges<T> ReadItemVectorChanges(FSWGPacket& Packet)
	{
		return ReadDeltaVectorChanges<T>(Packet, [](FSWGPacket& P)
		{
			T Item;
			Item.Deserialize(P);
			return Item;
		});
	}

	template<typename T>
	TSWGListChanges<T> ReadItemMapChanges(FSWGPacket& Packet)
	{
		return ReadDeltaVectorMapChanges<T>(Packet, [](FSWGPacket& P)
		{
			T Item;
			Item.Deserialize(P);
			return Item;
		});
	}

	TSWGListChanges<FString> ReadAsciiVectorChanges(FSWGPacket& Packet)
	{
		return ReadDeltaVectorChanges<FString>(Packet, [](FSWGPacket& P) { return P.ReadAsciiString(); });
	}
}

namespace SWGPlayerDeltaParser
{
	void ParseDelta3(FSWGPacket& Packet, FPlayerObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x05: Out.PlayerBitmasks = ReadBitmaskArray(P); return true;
				// Profile bitmasks: same shape, server sends four zeroes and
				// nothing reads them back.
				case 0x06: ReadBitmaskArray(P); return true;
				case 0x07: Out.Title = P.ReadAsciiString(); return true;
				case 0x08: Out.BirthDate = P.ReadInt32(); return true;
				case 0x09: Out.TotalPlayedTime = P.ReadInt32(); return true;
				default: return false;
			}
		});
	}

	void ParseDelta6(FSWGPacket& Packet, FPlayerObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x01: Out.PrivilegeFlag = P.ReadByte(); return true;
				default: return false;
			}
		});
	}

	void ParseDelta8(FSWGPacket& Packet, FPlayerObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x00: Out.ExperienceList = ReadItemMapChanges<FExperience>(P); return true;
				case 0x01: Out.WaypointList = ReadItemMapChanges<FWaypoint>(P); return true;
				case 0x02: Out.ForcePower = P.ReadInt32(); return true;
				case 0x03: Out.ForcePowerMax = P.ReadInt32(); return true;
				// Quest bit arrays are DeltaBitArray, itself a DeltaVector<byte>.
				case 0x04:
					Out.CompletedQuests = ReadDeltaVectorChanges<uint8>(P, [](FSWGPacket& Q) { return Q.ReadByte(); });
					return true;
				case 0x05:
					Out.ActiveQuests = ReadDeltaVectorChanges<uint8>(P, [](FSWGPacket& Q) { return Q.ReadByte(); });
					return true;
				case 0x06: Out.Quests = ReadItemMapChanges<FQuestJournalItem>(P); return true;
				default: return false;
			}
		});
	}

	void ParseDelta9(FSWGPacket& Packet, FPlayerObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x00: Out.AbilityList = ReadAsciiVectorChanges(P); return true;
				case 0x01: Out.ExperimentationFlag = P.ReadInt32(); return true;
				case 0x02: Out.CraftingState = P.ReadInt32(); return true;
				case 0x03: Out.ClosestCraftingStation = P.ReadInt64(); return true;
				case 0x04: Out.Schematics = ReadItemVectorChanges<FDraftSchematic>(P); return true;
				case 0x05: Out.ExperimentationPoints = P.ReadInt32(); return true;
				case 0x07: Out.FriendsList = ReadAsciiVectorChanges(P); return true;
				case 0x08: Out.IgnoreList = ReadAsciiVectorChanges(P); return true;
				case 0x09: Out.LanguageId = P.ReadInt32(); return true;
				case 0x0A: Out.FoodFilling = P.ReadInt32(); return true;
				case 0x0B: Out.FoodFillingMax = P.ReadInt32(); return true;
				case 0x0C: Out.DrinkFilling = P.ReadInt32(); return true;
				case 0x0D: Out.DrinkFillingMax = P.ReadInt32(); return true;
				case 0x11: Out.JediState = P.ReadInt32(); return true;
				default: return false;
			}
		});
	}
}
