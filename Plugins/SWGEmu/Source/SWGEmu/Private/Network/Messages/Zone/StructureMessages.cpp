#include "Network/Messages/Zone/StructureMessages.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/SWGMessageRegistry.h"

REGISTER_SWG_MESSAGE(FEnterStructurePlacementModeMessage, ESWGMessageOp::EnterStructurePlacementMode)

bool FEnterStructurePlacementModeMessage::Deserialize(FSWGMessage& Reader)
{
	DeedId = Reader.ReadUInt64();
	ClientTemplatePath = Reader.ReadAsciiString();
	UE_LOG(LogTemp, Log, TEXT("EnterStructurePlacementMode: deed=%llu template=%s"), DeedId, *ClientTemplatePath);
	return DeedId != 0 && ClientTemplatePath.StartsWith(TEXT("object/"));
}
