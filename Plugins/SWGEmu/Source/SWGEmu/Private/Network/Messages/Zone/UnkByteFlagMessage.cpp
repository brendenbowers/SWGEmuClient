#include "Network/Messages/Zone/UnkByteFlagMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FUnkByteFlagMessage, ESWGMessageOp::UnkByteFlag)

bool FUnkByteFlagMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> Value;
	return true;
}
