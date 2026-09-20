#include "Network/Objects/Zone/Mission/MissionObjectDelta.h"
#include "Network/Objects/Zone/Object/SWGDeltaListHelpers.h"

namespace SWGMissionDeltaParser
{
	void ParseDelta3(FSWGPacket& Packet, FMissionObjectDelta& Out, uint16 UpdateCount)
	{
		ReadDeltaUpdates(Packet, UpdateCount, [&Out](FSWGPacket& P, uint16 Index)
		{
			switch (Index)
			{
				case 0x05: Out.DifficultyDisplay = P.ReadInt32(); return true;
				case 0x06:
				{
					FVector Position;
					Position.X = P.ReadFloat();
					Position.Z = P.ReadFloat();
					Position.Y = P.ReadFloat();
					P.ReadInt64(); // target id, unused
					P.ReadUInt32(); // planet crc, unused here
					Out.EndPosition = Position;
					return true;
				}
				case 0x07: Out.CreatorName = P.ReadUnicodeString(); return true;
				case 0x08: Out.RewardCredits = P.ReadInt32(); return true;
				case 0x09:
				{
					FVector Position;
					Position.X = P.ReadFloat();
					Position.Z = P.ReadFloat();
					Position.Y = P.ReadFloat();
					P.ReadInt64();
					P.ReadUInt32();
					Out.StartPosition = Position;
					return true;
				}
				case 0x0A: Out.TargetTemplateCrc = P.ReadUInt32(); return true;
				case 0x0B: Out.MissionDescription = FSWGStringId::Read(P); return true;
				case 0x0C: Out.MissionTitle = FSWGStringId::Read(P); return true;
				case 0x0D: Out.RefreshCounter = P.ReadUInt32(); return true;
				case 0x0E: Out.TypeCRC = P.ReadUInt32(); return true;
				case 0x0F: Out.TargetName = P.ReadAsciiString(); return true;
				case 0x10: // the mission's granted waypoint — see FMissionObjectBaseline's own Waypoint* fields for the shape
				{
					Out.WaypointUnknown = P.ReadInt32();
					FVector Position;
					Position.X = P.ReadFloat();
					Position.Z = P.ReadFloat();
					Position.Y = P.ReadFloat();
					Out.WaypointPosition = Position;
					Out.WaypointTargetId = P.ReadInt64();
					Out.WaypointPlanetCrc = P.ReadUInt32();
					Out.WaypointName = P.ReadUnicodeString();
					Out.WaypointObjectId = P.ReadInt64();
					Out.WaypointColor = P.ReadByte();
					Out.WaypointActive = P.ReadByte();
					return true;
				}
				default: return false;
			}
		});
	}
}
