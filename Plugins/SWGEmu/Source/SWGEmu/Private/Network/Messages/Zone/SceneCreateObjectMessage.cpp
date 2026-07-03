#include "Network/Messages/Zone/SceneCreateObjectMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FSceneCreateObjectMessage, ESWGMessageOp::SceneCreateObjectByCrc)

bool FSceneCreateObjectMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> ObjectId;
	Reader >> DirX;
	Reader >> DirY;
	Reader >> DirZ;
	Reader >> DirW;
	Reader >> PosX;
	Reader >> PosZ;  // Z before Y on wire (SWG uses left-handed coords)
	Reader >> PosY;
	Reader >> ObjectCrc;
	Reader >> Hyperspacing;
	return true;
}
