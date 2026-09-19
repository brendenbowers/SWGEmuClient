#include "Network/Objects/Zone/Mission/MissionObjectBaseline.h"

namespace SWGMissionBaselineParser
{
	void ParseBase3(FSWGPacket& Packet, FMissionObjectBaseline& Out)
	{
		Out.UnknownVersion = Packet.ReadFloat();
		Out.ObjectName = FSWGStringId::Read(Packet);
		Out.CustomName = Packet.ReadUnicodeString();
		Out.Volume = Packet.ReadInt32();
		Out.Unused4 = Packet.ReadInt32();

		Out.DifficultyDisplay = Packet.ReadInt32();

		Out.EndPosition.X = Packet.ReadFloat();
		Out.EndPosition.Z = Packet.ReadFloat();
		Out.EndPosition.Y = Packet.ReadFloat();
		Out.EndObjectId = Packet.ReadInt64();
		Out.EndPlanetCrc = Packet.ReadUInt32();

		Out.CreatorName = Packet.ReadUnicodeString();
		Out.RewardCredits = Packet.ReadInt32();

		Out.StartPosition.X = Packet.ReadFloat();
		Out.StartPosition.Z = Packet.ReadFloat();
		Out.StartPosition.Y = Packet.ReadFloat();
		Out.StartObjectId = Packet.ReadInt64();
		Out.StartPlanetCrc = Packet.ReadUInt32();

		Out.TargetTemplateCrc = Packet.ReadUInt32();

		Out.MissionDescription = FSWGStringId::Read(Packet);
		Out.MissionTitle = FSWGStringId::Read(Packet);

		Out.RefreshCounter = Packet.ReadInt32();
		Out.TypeCRC = Packet.ReadUInt32();
		Out.TargetName = Packet.ReadAsciiString();

		Out.WaypointUnknown = Packet.ReadInt32();
		Out.WaypointPosition.X = Packet.ReadFloat();
		Out.WaypointPosition.Z = Packet.ReadFloat();
		Out.WaypointPosition.Y = Packet.ReadFloat();
		Out.WaypointTargetId = Packet.ReadInt64();
		Out.WaypointPlanetCrc = Packet.ReadUInt32();
		Out.WaypointName = Packet.ReadUnicodeString();
		Out.WaypointObjectId = Packet.ReadInt64();
		Out.WaypointColor = Packet.ReadByte();
		Out.WaypointActive = Packet.ReadByte();

		Out.bHasBase3 = true;
	}
}
