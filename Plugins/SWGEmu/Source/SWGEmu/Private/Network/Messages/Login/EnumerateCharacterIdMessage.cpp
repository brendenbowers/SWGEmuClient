#include "Network/Messages/Login/EnumerateCharacterIdMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FEnumerateCharacterIdMessage, ESWGMessageOp::EnumerateCharacterId)

bool FEnumerateCharacterIdMessage::Deserialize(FSWGMessage& Reader)
{
	int32 Count = 0;
	Reader >> Count;
	if (Count < 0 || Count > 10000)
	{
		UE_LOG(LogTemp, Warning, TEXT("EnumerateCharacterIdMessage: invalid character count %d"), Count);
		return false;
	}

	Characters.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		Characters.AddDefaulted_GetRef().Deserialize(Reader);
	}
	return true;
}
