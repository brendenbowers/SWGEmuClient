#include "BaselineHandlers/SWGCellBaselineHandler.h"

#include "Network/SWGPacket.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/SWGFourCC.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "Objects/World/SWGCell.h"
#include "SpawnHandlers/SWGBuildingSpawnHanlder.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Engine/GameInstance.h"

namespace
{
	// TLCS base3: the SceneObjectMessage3 fields, then cellNumber. Returns -1 when
	// there's no cell number to record.
	int32 ApplyCellBaseline(ASWGCell& Cell, uint8 Slot, FSWGPacket& Packet)
	{
		if (Slot != 3)
		{
			UE_LOG(LogTemp, Verbose, TEXT("FSWGCellBaselineHandler: no TLCS baseline dispatch for slot %d"), Slot);
			return -1;
		}

		Packet.ReadFloat(); // complexity
		const FSWGStringId ObjectName = FSWGStringId::Read(Packet);
		const FString CustomName = Packet.ReadUnicodeString();
		Packet.ReadInt32(); // volume

		Cell.CellName = CustomName.IsEmpty() ? ObjectName.StringTableId : CustomName;

		return Packet.ReadInt32(); // cellNumber
	}
}

bool FSWGCellBaselineHandler::CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const
{
	// the filter on the message type should be enough, this may need to be updated in the future
	return true;
}

bool FSWGCellBaselineHandler::HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args)
{
	ASWGCell* CellActor = Cast<ASWGCell>(&Actor);
	UGameInstance* GameInstance = Actor.GetWorld() ? Actor.GetWorld()->GetGameInstance() : nullptr;
	if (!CellActor || !GameInstance || !Args.ObjectGraph)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCellBaselineHandler: SCLT baseline for object %lld landed on %s — skipping slot %d"),
			Msg.ObjectId, *Actor.GetClass()->GetName(), Msg.BaselineType);
		return false;
	}

	FSWGPacket Packet = Msg.AsPayloadPacket();

	const int32 CellNumber = ApplyCellBaseline(*CellActor, Msg.BaselineType, Packet);
	if (CellNumber >= 0)
	{
		UE_LOG(LogTemp, Log, TEXT("FSWGCellBaselineHandler: cell %lld is number %d, name '%s'"),
			Msg.ObjectId, CellNumber, *CellActor->CellName);

		// The cell can't be finished until its containment has arrived too, so this
		// only completes whichever of the two lands second.
		Args.ObjectGraph->SetCellNumber(Msg.ObjectId, CellNumber);
		FSWGCellSpawnHandler::CheckAndFinishCell(*Args.ObjectGraph, Msg.ObjectId,
			GameInstance->GetSubsystem<USWGTreSubsystem>(), GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>());
	}

	return true;
}

REGISTER_SWG_BASELINE_HANDLER(FSWGCellBaselineHandler, ESWGObjectType::SCLT)
