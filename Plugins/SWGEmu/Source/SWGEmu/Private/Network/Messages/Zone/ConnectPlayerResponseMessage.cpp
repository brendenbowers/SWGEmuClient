#include "Network/Messages/Zone/ConnectPlayerResponseMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FConnectPlayerResponseMessage, ESWGMessageOp::ConnectPlayerResponseMessage)

bool FConnectPlayerResponseMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> Unknown;
	return true;
}
