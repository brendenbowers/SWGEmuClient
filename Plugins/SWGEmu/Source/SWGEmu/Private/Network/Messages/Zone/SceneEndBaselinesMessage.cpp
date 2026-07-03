#include "Network/Messages/Zone/SceneEndBaselinesMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FSceneEndBaselinesMessage, ESWGMessageOp::SceneEndBaselines)

bool FSceneEndBaselinesMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> ObjectId;
	return true;
}
