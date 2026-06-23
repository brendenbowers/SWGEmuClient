#include "Network/Messages/Login/LoginEnumClusterMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FLoginEnumClusterMessage, ESWGMessageOp::LoginEnumCluster)

bool FLoginEnumClusterMessage::Deserialize(FSWGMessage& Reader)
{
	int32 Count = 0;
	Reader >> Count;
	if (Count < 0 || Count > 1000)
	{
		UE_LOG(LogTemp, Warning, TEXT("LoginEnumClusterMessage: invalid server count %d"), Count);
		return false;
	}

	Servers.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		FServerName& S = Servers.AddDefaulted_GetRef();
		Reader >> S.ServerID;
		S.ServerDisplayName = Reader.ReadAsciiString();
		Reader >> S.Timezone;
	}

	Reader >> MaxCharactersPerAccount;
	return true;
}
