#include "Network/Messages/Zone/UpdateContainmentMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FUpdateContainmentMessage, ESWGMessageOp::UpdateContainmentMessage)

bool FUpdateContainmentMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> ObjectId;
	Reader >> ContainerId;
	Reader >> Type;
	return true;
}
