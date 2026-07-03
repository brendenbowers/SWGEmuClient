#include "Network/Objects/Zone/Player/PlayerObjectBaseline.h"

namespace SWGPlayerBaselineParser
{
	void ParseBase3(FSWGPacket& Packet, FPlayerObjectBaseline& Out)
	{
		// IntangibleObjectMessage3 fields.
		Out.Complexity = Packet.ReadFloat();
		Out.ObjectName = FSWGStringId::Read(Packet);
		Out.CustomName = Packet.ReadUnicodeString();
		Out.Volume = Packet.ReadInt32();
		Out.Status = Packet.ReadInt32();

		// PlayerObjectMessage3 additions.
		Packet.ReadInt32(); // Bitmask array count, always 4
		Out.PlayerBitmasks.SetNum(4);
		for (int32 i = 0; i < 4; ++i)
		{
			Out.PlayerBitmasks[i] = Packet.ReadUInt32();
		}

		Packet.ReadInt32(); // Profile settings count, always 4
		for (int32 i = 0; i < 4; ++i)
		{
			Packet.ReadInt32(); // Unused profile setting slots
		}

		Out.Title = Packet.ReadAsciiString();
		Out.BirthDate = Packet.ReadInt32();
		Out.TotalPlayedTime = Packet.ReadInt32();

		Packet.ReadInt32(); // Fixed 0x6C2
		Packet.ReadInt32(); // Fixed 0xDC62
		Packet.ReadInt32(); // Fixed 0x23

		Out.bHasBase3 = true;
	}

	void ParseBase6(FSWGPacket& Packet, FPlayerObjectBaseline& Out)
	{
		Packet.ReadInt32(); // Fixed 0
		Out.PrivilegeFlag = Packet.ReadByte();
		Out.bHasBase6 = true;
	}

	void ParseBase8(FSWGPacket& Packet, FPlayerObjectBaseline& Out)
	{
		Out.ExperienceList = ReadBaselineMap<FExperience>(Packet, [](FSWGPacket& P)
		{
			FExperience Xp;
			Xp.Deserialize(P);
			return Xp;
		});

		Out.WaypointList = ReadBaselineMap<FWaypoint>(Packet, [](FSWGPacket& P)
		{
			FWaypoint WP;
			WP.MapKey = P.ReadUInt64();
			WP.Deserialize(P);
			return WP;
		});

		Out.ForcePower = Packet.ReadInt32();
		Out.ForcePowerMax = Packet.ReadInt32();

		Out.CompletedQuests = ReadBaselineVector<uint8>(Packet, [](FSWGPacket& P) { return P.ReadByte(); });
		Out.ActiveQuests = ReadBaselineVector<uint8>(Packet, [](FSWGPacket& P) { return P.ReadByte(); });

		Out.Quests = ReadBaselineMap<FQuestJournalItem>(Packet, [](FSWGPacket& P)
		{
			FQuestJournalItem Quest;
			Quest.Deserialize(P);
			return Quest;
		});

		Packet.ReadInt32(); // Reserved
		Packet.ReadInt32(); // Reserved

		Out.bHasBase8 = true;
	}

	void ParseBase9(FSWGPacket& Packet, FPlayerObjectBaseline& Out)
	{
		Out.AbilityList = ReadBaselineVector<FString>(Packet, [](FSWGPacket& P) { return P.ReadAsciiString(); });

		Packet.ReadInt32(); // Crafting state, always 0
		Packet.ReadInt32(); // Crafting state, always 0
		Packet.ReadInt64(); // Nearest crafting station id, always 0

		Out.Schematics = ReadBaselineVector<FDraftSchematic>(Packet, [](FSWGPacket& P)
		{
			FDraftSchematic Schematic;
			Schematic.Deserialize(P);
			return Schematic;
		});

		Packet.ReadInt32(); // Crafting-related, always 0
		Packet.ReadInt32(); // Species data, always 0

		Packet.ReadInt32(); // Friends list count, always 0 (not yet implemented server-side)
		Packet.ReadInt32(); // Friends list update counter, always 0
		Packet.ReadInt32(); // Ignore list count, always 0 (not yet implemented server-side)
		Packet.ReadInt32(); // Ignore list update counter, always 0

		Out.LanguageId = Packet.ReadInt32();

		Out.FoodFilling = Packet.ReadInt32();
		Out.FoodFillingMax = Packet.ReadInt32();
		Out.DrinkFilling = Packet.ReadInt32();
		Out.DrinkFillingMax = Packet.ReadInt32();

		Packet.ReadInt32(); // Reserved
		Packet.ReadInt32(); // Reserved
		Packet.ReadInt32(); // Waypoint-related placeholder count, always 0
		Packet.ReadInt32(); // Waypoint-related placeholder update counter, always 0

		Out.JediState = Packet.ReadInt32();

		Out.bHasBase9 = true;
	}
}
