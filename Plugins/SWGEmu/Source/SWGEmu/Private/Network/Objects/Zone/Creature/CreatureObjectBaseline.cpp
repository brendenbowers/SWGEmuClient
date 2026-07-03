#include "Network/Objects/Zone/Creature/CreatureObjectBaseline.h"

namespace SWGCreatureBaselineParser
{
	void ParseBase1(FSWGPacket& Packet, FCreatureObjectBaseline& Out)
	{
		Out.BankCredits = Packet.ReadInt32();
		Out.CashCredits = Packet.ReadInt32();

		Out.BaseHAM = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });

		Out.SkillList = ReadBaselineVector<FString>(Packet, [](FSWGPacket& P) { return P.ReadAsciiString(); });

		Out.bHasBase1 = true;
	}

	void ParseBase3(FSWGPacket& Packet, FCreatureObjectBaseline& Out)
	{
		// TangibleObjectMessage3 fields come first on the wire.
		SWGTangibleBaselineParser::ParseBase3(Packet, Out.Tangible);

		Out.Posture = Packet.ReadByte();
		Out.FactionRank = Packet.ReadByte();
		Out.CreatureLinkId = Packet.ReadInt64();
		Out.Height = Packet.ReadFloat();
		Out.ShockWounds = Packet.ReadInt32();
		Out.StateBitmask = Packet.ReadInt64();

		Out.Wounds = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });

		Out.bHasBase3 = true;
	}

	void ParseBase4(FSWGPacket& Packet, FCreatureObjectBaseline& Out)
	{
		Out.AccelerationMultiplierBase = Packet.ReadFloat();
		Out.AccelerationMultiplierMod = Packet.ReadFloat();

		Out.Encumbrances = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });

		Out.SkillMods = ReadBaselineMap<FSkillModifier>(Packet, [](FSWGPacket& P)
		{
			FSkillModifier Mod;
			Mod.Deserialize(P);
			return Mod;
		});

		Out.SpeedMultiplierBase = Packet.ReadFloat();
		Out.SpeedMultiplierMod = Packet.ReadFloat();
		Out.ListenId = Packet.ReadInt64();
		Out.RunSpeed = Packet.ReadFloat();
		Out.SlopeModAngle = Packet.ReadFloat();
		Out.SlopeModPercent = Packet.ReadFloat();
		Out.TurnScale = Packet.ReadFloat();
		Out.WalkSpeed = Packet.ReadFloat();
		Out.WaterModPercent = Packet.ReadFloat();

		Out.SpaceMissionObjects = ReadBaselineVector<FGroupMissionCriticalObject>(Packet, [](FSWGPacket& P)
		{
			FGroupMissionCriticalObject Obj;
			Obj.Deserialize(P);
			return Obj;
		});

		Out.bHasBase4 = true;
	}

	void ParseBase6(FSWGPacket& Packet, FCreatureObjectBaseline& Out)
	{
		// TangibleObjectMessage6 fields come first on the wire.
		SWGTangibleBaselineParser::ParseBase6(Packet, Out.Tangible);

		Out.Level = Packet.ReadUInt16();
		Out.PerformanceAnimation = Packet.ReadAsciiString();
		Out.MoodString = Packet.ReadAsciiString();
		Out.WeaponId = Packet.ReadInt64();
		Out.GroupId = Packet.ReadInt64();
		Out.GroupInviterId = Packet.ReadInt64();
		Out.GroupInviteCounter = Packet.ReadInt64();
		Out.GuildId = Packet.ReadInt32();
		Out.TargetId = Packet.ReadInt64();
		Out.MoodId = Packet.ReadByte();
		Out.PerformanceStartTime = Packet.ReadInt32();
		Out.PerformanceType = Packet.ReadInt32();

		Out.HAM = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });
		Out.MaxHAM = ReadBaselineVector<int32>(Packet, [](FSWGPacket& P) { return P.ReadInt32(); });

		Out.EquipmentList = ReadBaselineVector<FEquiptmentItem>(Packet, [](FSWGPacket& P)
		{
			FEquiptmentItem Item;
			Item.Deserialize(P);
			return Item;
		});

		Out.AlternateAppearance = Packet.ReadAsciiString();
		Out.Frozen = Packet.ReadByte();

		Out.bHasBase6 = true;
	}
}
