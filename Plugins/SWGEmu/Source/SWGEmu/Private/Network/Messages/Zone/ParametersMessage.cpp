#include "Network/Messages/Zone/ParametersMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FParametersMessage, ESWGMessageOp::ParametersMessage)

bool FParametersMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> Value;
	return true;
}
