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
	Reader >> TickCount;

	// Whatever remains belongs to the sub-message named by Type.
	const int32 Remaining = Reader.GetRemaining();
	if (Remaining > 0)
	{
		RawPayload.SetNumUninitialized(Remaining);
		Reader.Serialize(RawPayload.GetData(), Remaining);
	}

	return true;
}
