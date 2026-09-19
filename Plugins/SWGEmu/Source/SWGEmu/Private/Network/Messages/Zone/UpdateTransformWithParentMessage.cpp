#include "Network/Messages/Zone/UpdateTransformWithParentMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FUpdateTransformWithParentMessage, ESWGMessageOp::UpdateTransformMessageWithParent)

bool FUpdateTransformWithParentMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> ParentId;
	Reader >> ObjectId;

	int16 RawX = 0, RawZ = 0, RawY = 0;
	Reader >> RawX;
	Reader >> RawZ;
	Reader >> RawY;
	PosX = RawX / 8.0f;
	PosZ = RawZ / 8.0f;
	PosY = RawY / 8.0f;

	Reader >> MovementCounter;

	uint8 SpeedByte = 0;
	Reader >> SpeedByte;
	CurrentSpeed = static_cast<int8>(SpeedByte);

	Reader >> DirectionAngle;

	return true;
}
