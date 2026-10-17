#include "Network/Messages/Zone/CreateMissileMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/SWGMessageRegistry.h"

REGISTER_SWG_MESSAGE(FCreateMissileMessage, ESWGMessageOp::CreateMissileMessage)

bool FCreateMissileMessage::Deserialize(FSWGMessage& Reader)
{
	if (Reader.GetRemaining() != 60) return false;
	MissileId = Reader.ReadInt32();
	SourceId = int64(Reader.ReadUInt64());
	TargetId = int64(Reader.ReadUInt64());
	for (int32 CoordinateIndex = 0; CoordinateIndex < 6; ++CoordinateIndex) Reader.ReadFloat();
	SecondsToHit = Reader.ReadInt32();
	MissileType = Reader.ReadInt32();
	WeaponSlot = Reader.ReadInt32();
	TargetSlot = Reader.ReadInt32();
	return SourceId != 0 && TargetId != 0 && SecondsToHit >= 0;
}
