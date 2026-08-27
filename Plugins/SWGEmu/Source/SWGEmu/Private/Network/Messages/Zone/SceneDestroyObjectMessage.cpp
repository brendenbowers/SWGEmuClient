#include "Network/Messages/Zone/SceneDestroyObjectMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FSceneDestroyObjectMessage, ESWGMessageOp::SceneDestroyObject)

bool FSceneDestroyObjectMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> ObjectId;

	// Older servers end the message here.
	if (Reader.GetRemaining() > 0)
	{
		Reader >> bHyperspace;
	}

	return true;
}
