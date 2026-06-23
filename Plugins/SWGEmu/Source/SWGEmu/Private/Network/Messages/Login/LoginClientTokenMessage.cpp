#include "Network/Messages/Login/LoginClientTokenMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FLoginClientTokenMessage, ESWGMessageOp::LoginClientToken)

bool FLoginClientTokenMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> SessionKeySize;
	if (SessionKeySize < 0 || SessionKeySize > 512)
	{
		UE_LOG(LogTemp, Warning, TEXT("LoginClientTokenMessage: invalid SessionKeySize %d"), SessionKeySize);
		return false;
	}

	SessionKey.SetNum(SessionKeySize);
	Reader.Serialize(SessionKey.GetData(), SessionKeySize);

	Reader >> StationID;
	UserName = Reader.ReadAsciiString();
	return true;
}
