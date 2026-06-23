#include "Network/Messages/Zone/ClientPermissionsMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FClientPermissionsMessage, ESWGMessageOp::ClientPermissionsMessage)

bool FClientPermissionsMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> GalaxyOpen;
	Reader >> CanCreateChar;
	Reader >> UnlimitedCreation;
	Reader >> UnknownFlag;
	return true;
}
