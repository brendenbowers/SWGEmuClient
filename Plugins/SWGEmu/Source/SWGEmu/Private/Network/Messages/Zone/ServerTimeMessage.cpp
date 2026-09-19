#include "Network/Messages/Zone/ServerTimeMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FServerTimeMessage, ESWGMessageOp::ServerTime)

bool FServerTimeMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> GalacticTime;
	return true;
}
