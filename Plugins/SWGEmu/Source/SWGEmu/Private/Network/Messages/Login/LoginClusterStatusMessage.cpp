#include "Network/Messages/Login/LoginClusterStatusMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FLoginClusterStatusMessage, ESWGMessageOp::LoginClusterStatus)

bool FLoginClusterStatusMessage::Deserialize(FSWGMessage& Reader)
{
	uint32 Count = 0;
	Reader >> Count;
	if (Count > 1000)
	{
		UE_LOG(LogTemp, Warning, TEXT("LoginClusterStatusMessage: implausible server count %u"), Count);
		return false;
	}

	Servers.Reserve(Count);
	for (uint32 i = 0; i < Count; ++i)
	{
		FServerDetails& S = Servers.AddDefaulted_GetRef();
		Reader >> S.ServerID;
		S.ServerIP = Reader.ReadAsciiString();
		Reader >> S.ServerPort
		       >> S.PingPort
		       >> S.Population
		       >> S.MaxCapacity
		       >> S.MaxCharsPerServer
		       >> S.Distance
		       >> S.Status;
		uint8 NotRec = 0;
		Reader >> NotRec;
		S.bNotRecommended = NotRec != 0;
	}
	return true;
}

const FServerDetails* FLoginClusterStatusMessage::FindServer(uint32 ServerID) const
{
	return Servers.FindByPredicate([ServerID](const FServerDetails& S) { return S.ServerID == ServerID; });
}
