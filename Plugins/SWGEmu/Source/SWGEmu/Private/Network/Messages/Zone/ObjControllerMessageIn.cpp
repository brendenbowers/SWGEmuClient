#include "Network/Messages/Zone/ObjControllerMessageIn.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FObjControllerMessageIn, ESWGMessageOp::ObjControllerMessage)

bool FObjControllerMessageIn::Deserialize(FSWGMessage& Reader)
{
	Reader >> Priority;
	Reader >> Type;
	Reader >> ObjectId;
	return true;
}
