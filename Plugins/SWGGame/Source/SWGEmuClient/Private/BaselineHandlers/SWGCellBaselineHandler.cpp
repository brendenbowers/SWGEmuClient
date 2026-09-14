#include "BaselineHandlers/SWGCellBaselineHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Cell/CellObjectBaseline.h"
#include "Objects/World/SWGCell.h"
#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Engine/GameInstance.h"

bool FSWGCellBaselineHandler::CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const
{
	// the filter on the message type should be enough, this may need to be updated in the future
	return true;
}

bool FSWGCellBaselineHandler::HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args)
{
	if (Msg.BaselineType != 3)
	{
		UE_LOG(LogTemp, Verbose, TEXT("FSWGCellBaselineHandler: no TLCS baseline dispatch for slot %d"), Msg.BaselineType);
		return true;
	}

	FSWGPacket Packet = Msg.AsPayloadPacket();
	FCellObjectBaseline Baseline;
	SWGCellBaselineParser::ParseBase3(Packet, Baseline);

	if (ASWGCell* CellActor = Cast<ASWGCell>(&Actor))
	{
		CellActor->CellName = Baseline.CustomName.IsEmpty() ? Baseline.ObjectName.StringTableId : Baseline.CustomName;
	}

	// The cell can't be finished until its containment has arrived too, so this
	// only completes whichever of the two lands second.
	UGameInstance* GameInstance = Actor.GetWorld() ? Actor.GetWorld()->GetGameInstance() : nullptr;
	if (Args.ObjectGraph && GameInstance)
	{
		Args.ObjectGraph->SetCellNumber(Msg.ObjectId, Baseline.CellNumber);
		FSWGCellSpawnHandler::CheckAndFinishCell(*Args.ObjectGraph, Msg.ObjectId,
			GameInstance->GetSubsystem<USWGTreSubsystem>(), GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>());
	}

	return true;
}

REGISTER_SWG_BASELINE_HANDLER(FSWGCellBaselineHandler, ESWGObjectType::SCLT)
