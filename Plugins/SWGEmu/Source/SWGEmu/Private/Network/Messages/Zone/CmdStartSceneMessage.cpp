#include "Network/Messages/Zone/CmdStartSceneMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "Network/Messages/SWGMessageOp.h"

REGISTER_SWG_MESSAGE(FCmdStartSceneMessage, ESWGMessageOp::CmdStartScene)

bool FCmdStartSceneMessage::Deserialize(FSWGMessage& Reader)
{
	Reader >> IgnoreLayout;
	Reader >> CharacterID;
	TerrainName = Reader.ReadAsciiString();
	Reader >> PosX;
	Reader >> PosZ;  // native order is x, y-up, z; stored in raw (Core3) order
	Reader >> PosY;
	RaceTemplate = Reader.ReadAsciiString();
	Reader >> GalacticTime;

	UE_LOG(LogTemp, Log, TEXT("FCmdStartSceneMessage::Deserialize: TerrainName='%s' (Len=%d) CharacterID=%lld"),
		*TerrainName, TerrainName.Len(), CharacterID);

	return true;
}
