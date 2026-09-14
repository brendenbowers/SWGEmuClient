#include "DeltaHandlers/SWGCellDeltaHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Cell/CellObjectDelta.h"
#include "Objects/World/SWGCell.h"
#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Engine/GameInstance.h"

bool FSWGCellDeltaHandler::CanHandleDelta(const AActor& Actor, const FDeltasMessage& Msg) const
{
	return true;
}

bool FSWGCellDeltaHandler::HandleDelta(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments& Args)
{
	if (Msg.DeltaType != 3)
	{
		UE_LOG(LogTemp, Verbose, TEXT("FSWGCellDeltaHandler: no TLCS delta dispatch for slot %d"), Msg.DeltaType);
		return true;
	}

	FSWGPacket Packet = Msg.AsPayloadPacket();
	FCellObjectDelta Delta;
	SWGCellDeltaParser::ParseDelta3(Packet, Delta, Msg.UpdateCount);

	UGameInstance* GameInstance = Actor.GetWorld() ? Actor.GetWorld()->GetGameInstance() : nullptr;
	if (Delta.CellNumber.IsSet() && Args.ObjectGraph && GameInstance)
	{
		Args.ObjectGraph->SetCellNumber(Msg.ObjectId, *Delta.CellNumber);
		FSWGCellSpawnHandler::CheckAndFinishCell(*Args.ObjectGraph, Msg.ObjectId,
			GameInstance->GetSubsystem<USWGTreSubsystem>(), GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>());
	}

	return true;
}

REGISTER_SWG_DELTA_HANDLER(FSWGCellDeltaHandler, ESWGObjectType::SCLT)
