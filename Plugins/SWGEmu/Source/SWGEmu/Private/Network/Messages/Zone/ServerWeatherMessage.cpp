#include "Network/Messages/Zone/ServerWeatherMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/SWGMessageRegistry.h"

REGISTER_SWG_MESSAGE(FServerWeatherMessage, ESWGMessageOp::ServerWeatherMessage)

bool FServerWeatherMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> Weather >> WindX >> WindUp >> WindZ;
	return true;
}
