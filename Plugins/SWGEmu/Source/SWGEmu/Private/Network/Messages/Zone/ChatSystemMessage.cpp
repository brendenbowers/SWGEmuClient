#include "Network/Messages/Zone/ChatSystemMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FChatSystemMessage, ESWGMessageOp::ChatSystemMessage)

bool FChatSystemMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> DisplayType;
	Message = Reader.ReadUnicodeString();
	return true;
}
