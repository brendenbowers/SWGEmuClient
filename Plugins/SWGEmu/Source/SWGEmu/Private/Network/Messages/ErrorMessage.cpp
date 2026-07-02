#include "Network/Messages/ErrorMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FErrorMessage, ESWGMessageOp::ErrorMessage)

bool FErrorMessage::Deserialize(FSWGMessage& Reader)
{
	ErrorType = Reader.ReadAsciiString();
	ErrorMsg  = Reader.ReadAsciiString();
	Reader >> Fatal;
	return true;
}
