#include "Network/Objects/Zone/Creature/CreatureObjectDelta.h"

namespace
{
	TSWGListChanges<FSkillModifier> ReadSkillModChanges(FSWGPacket& Packet)
	{
		return ReadDeltaVectorMapChanges<FSkillModifier>(Packet, [](FSWGPacket& P)
		{
			FSkillModifier Mod;
			Mod.Deserialize(P);
			return Mod;
		});
	}

	TSWGListChanges<FGroupMissionCriticalObject> ReadSpaceMissionObjectChanges(FSWGPacket& Packet)
	{
		return ReadDeltaSetChanges<FGroupMissionCriticalObject>(Packet, [](FSWGPacket& P)
		{
			FGroupMissionCriticalObject Obj;
			Obj.Deserialize(P);
			return Obj;
		});
	}

	TSWGListChanges<FEquiptmentItem> ReadEquipmentChanges(FSWGPacket& Packet)
	{
		return ReadDeltaVectorChanges<FEquiptmentItem>(Packet, [](FSWGPacket& P)
		{
			FEquiptmentItem Item;
			Item.Deserialize(P);
			return Item;
		});
	}
}

namespace SWGCreatureDeltaParser
{
	void ParseDelta1(FSWGPacket& Packet, FCreatureObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x00: Out.BankCredits = P.ReadInt32(); return true;
				case 0x01: Out.CashCredits = P.ReadInt32(); return true;
				case 0x02: Out.BaseHAM = ReadInt32DeltaVectorChanges(P); return true;
				case 0x03:
					Out.SkillList = ReadDeltaVectorChanges<FString>(P, [](FSWGPacket& Q) { return Q.ReadAsciiString(); });
					return true;
				default: return false;
			}
		});
	}

	void ParseDelta3(FSWGPacket& Packet, FCreatureObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				// Shares the tangible slot that holds UseCount, but for a creature
				// the server writes incapacitation recovery time into it.
				case 0x07: Out.IncapacitationRecoveryTime = P.ReadInt32(); return true;
				case 0x0B: Out.Posture = P.ReadByte(); return true;
				case 0x0C: Out.FactionRank = P.ReadByte(); return true;
				case 0x0D: Out.CreatureLinkId = P.ReadInt64(); return true;
				case 0x0E: Out.Height = P.ReadFloat(); return true;
				case 0x0F: Out.ShockWounds = P.ReadInt32(); return true;
				case 0x10: Out.StateBitmask = P.ReadInt64(); return true;
				case 0x11: Out.Wounds = ReadInt32DeltaVectorChanges(P); return true;
				default: return SWGTangibleDeltaParser::ApplyUpdate3(P, Out.Tangible, Index);
			}
		});
	}

	void ParseDelta4(FSWGPacket& Packet, FCreatureObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x00: Out.AccelerationMultiplierBase = P.ReadFloat(); return true;
				case 0x01: Out.AccelerationMultiplierMod = P.ReadFloat(); return true;
				case 0x02: Out.Encumbrances = ReadInt32DeltaVectorChanges(P); return true;
				case 0x03: Out.SkillMods = ReadSkillModChanges(P); return true;
				case 0x04: Out.SpeedMultiplierBase = P.ReadFloat(); return true;
				case 0x05: Out.SpeedMultiplierMod = P.ReadFloat(); return true;
				case 0x06: Out.ListenId = P.ReadInt64(); return true;
				case 0x07: Out.RunSpeed = P.ReadFloat(); return true;
				case 0x08: Out.SlopeModAngle = P.ReadFloat(); return true;
				case 0x09: Out.SlopeModPercent = P.ReadFloat(); return true;
				case 0x0A: Out.TurnScale = P.ReadFloat(); return true;
				case 0x0B: Out.WalkSpeed = P.ReadFloat(); return true;
				case 0x0C: Out.WaterModPercent = P.ReadFloat(); return true;
				case 0x0D: Out.SpaceMissionObjects = ReadSpaceMissionObjectChanges(P); return true;
				default: return false;
			}
		});
	}

	void ParseDelta6(FSWGPacket& Packet, FCreatureObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x02: Out.Level = P.ReadUInt16(); return true;
				case 0x03: Out.PerformanceAnimation = P.ReadAsciiString(); return true;
				case 0x04: Out.MoodString = P.ReadAsciiString(); return true;
				case 0x05: Out.WeaponId = P.ReadInt64(); return true;
				case 0x06: Out.GroupId = P.ReadInt64(); return true;
				case 0x07:
					Out.GroupInviterId = P.ReadInt64();
					Out.GroupInviteCounter = P.ReadInt64();
					return true;
				case 0x08: Out.GuildId = P.ReadInt32(); return true;
				case 0x09: Out.TargetId = P.ReadInt64(); return true;
				case 0x0A: Out.MoodId = P.ReadByte(); return true;
				case 0x0B: Out.PerformanceStartTime = P.ReadInt32(); return true;
				case 0x0C: Out.PerformanceType = P.ReadInt32(); return true;
				case 0x0D: Out.HAM = ReadInt32DeltaVectorChanges(P); return true;
				case 0x0E: Out.MaxHAM = ReadInt32DeltaVectorChanges(P); return true;
				case 0x0F: Out.EquipmentList = ReadEquipmentChanges(P); return true;
				case 0x10: Out.AlternateAppearance = P.ReadAsciiString(); return true;
				default: return SWGTangibleDeltaParser::ApplyUpdate6(P, Out.Tangible, Index);
			}
		});
	}
}
