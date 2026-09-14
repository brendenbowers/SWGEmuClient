#include "Network/Messages/Zone/SuiPageMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FSuiCreatePageMessage, ESWGMessageOp::SuiCreatePage)
REGISTER_SWG_MESSAGE(FSuiUpdatePageMessage, ESWGMessageOp::SuiUpdatePage)
REGISTER_SWG_MESSAGE(FSuiForceClosePageMessage, ESWGMessageOp::SuiForceClosePage)

bool FSuiCreatePageMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> PageId;
	ScriptClass = Reader.ReadAsciiString();

	uint32 CommandCount = 0;
	Reader >> CommandCount;
	if (CommandCount > 4096)
	{
		return false;
	}

	Commands.Reset(CommandCount);
	for (uint32 CommandIndex = 0; CommandIndex < CommandCount && Reader.GetRemaining() > 0; ++CommandIndex)
	{
		FSWGSuiCommand& Command = Commands.AddDefaulted_GetRef();
		Reader >> Command.Type;

		uint32 WideCount = 0;
		Reader >> WideCount;
		for (uint32 ParamIndex = 0; ParamIndex < WideCount && Reader.GetRemaining() > 0; ++ParamIndex)
		{
			Command.WideParams.Add(Reader.ReadUnicodeString());
		}

		uint32 NarrowCount = 0;
		Reader >> NarrowCount;
		for (uint32 ParamIndex = 0; ParamIndex < NarrowCount && Reader.GetRemaining() > 0; ++ParamIndex)
		{
			Command.NarrowParams.Add(Reader.ReadAsciiString());
		}
	}

	// Footer. The legacy SuiBox writer has a shorter variant, so stop
	// wherever the bytes run out rather than fail the whole page.
	if (Reader.GetRemaining() >= 12)
	{
		Reader >> UsingObjectId;
		Reader >> ForceCloseDistance;
	}
	return true;
}

bool FSuiForceClosePageMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> PageId;
	return true;
}
