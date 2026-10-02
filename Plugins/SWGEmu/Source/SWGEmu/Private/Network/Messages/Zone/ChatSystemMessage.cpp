#include "Network/Messages/Zone/ChatSystemMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FChatSystemMessage, ESWGMessageOp::ChatSystemMessage)

bool FChatSystemMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> DisplayType;
	Message = Reader.ReadUnicodeString();
	if (Reader.GetRemaining() >= 4 && Reader.ReadUInt32() > 0)
	{
		if (Reader.GetRemaining() < 7) { return false; }
		Reader.Skip(7); // ChatParameter: padding short, type byte, sentinel int
		const FString File = Reader.ReadAsciiString();
		if (Reader.GetRemaining() >= 4)
		{
			Reader.ReadUInt32(); // StringId filler
			const FString Key = Reader.ReadAsciiString();
			if (!File.IsEmpty() && !Key.IsEmpty()) { Message = FString::Printf(TEXT("@%s:%s"), *File, *Key); }
		}
	}
	return true;
}
