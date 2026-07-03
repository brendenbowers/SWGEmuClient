#include "Subsystems/SWGObjectStoreSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Network/SWGPacket.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/SceneCreateObjectMessage.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/Zone/SceneEndBaselinesMessage.h"
#include "Network/Messages/Zone/DeltasMessage.h"

void USWGObjectStoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Network = Cast<USWGNetworkSubsystem>(
		Collection.InitializeDependency(USWGNetworkSubsystem::StaticClass()));

	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(
			this, &USWGObjectStoreSubsystem::HandleMessageReceived);
	}

	UE_LOG(LogTemp, Log, TEXT("SWGObjectStoreSubsystem: initialized"));
}

void USWGObjectStoreSubsystem::Deinitialize()
{
	if (Network && MessageHandle.IsValid())
	{
		Network->OnMessageReceived.Remove(MessageHandle);
		MessageHandle.Reset();
	}

	Objects.Empty();

	Super::Deinitialize();
	UE_LOG(LogTemp, Log, TEXT("SWGObjectStoreSubsystem: deinitialized"));
}

const FSWGObjectRecord* USWGObjectStoreSubsystem::FindObject(int64 ObjectId) const
{
	if (const TSharedPtr<FSWGObjectRecord>* Found = Objects.Find(ObjectId))
	{
		return Found->Get();
	}
	return nullptr;
}

FSWGObjectRecord& USWGObjectStoreSubsystem::FindOrAdd(int64 ObjectId)
{
	TSharedPtr<FSWGObjectRecord>& Record = Objects.FindOrAdd(ObjectId);
	if (!Record.IsValid())
	{
		Record = MakeShared<FSWGObjectRecord>();
		Record->ObjectId = ObjectId;
	}
	return *Record;
}

void USWGObjectStoreSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Msg)
{
	if (!Msg.IsValid())
		return;

	const uint32 Opcode = Msg->Opcode;

	if (Opcode == static_cast<uint32>(ESWGMessageOp::SceneCreateObjectByCrc))
	{
		const auto* Create = static_cast<const FSceneCreateObjectMessage*>(Msg.Get());

		FSWGObjectRecord& Record = FindOrAdd(Create->ObjectId);
		Record.ObjectCrc = Create->ObjectCrc;
		Record.PosX = Create->PosX;
		Record.PosY = Create->PosY;
		Record.PosZ = Create->PosZ;
		Record.DirX = Create->DirX;
		Record.DirY = Create->DirY;
		Record.DirZ = Create->DirZ;
		Record.DirW = Create->DirW;
		Record.bHyperspacing = Create->Hyperspacing != 0;
		Record.bReady = false;
	}
	else if (Opcode == static_cast<uint32>(ESWGMessageOp::BaselinesMessage))
	{
		const auto* Baseline = static_cast<const FBaselinesMessage*>(Msg.Get());

		FSWGObjectRecord& Record = FindOrAdd(Baseline->ObjectId);
		FSWGPacket Sub(Baseline->RawPayload.GetData(), Baseline->RawPayload.Num());
		const FString FourCC = Baseline->GetObjectTypeFourCC();

		if (FourCC == TEXT("TANO"))
		{
			if (!Record.Tangible.IsSet())
				Record.Tangible = FTangibleObjectBaseline();

			if (Baseline->BaselineType == 3)
				SWGTangibleBaselineParser::ParseBase3(Sub, Record.Tangible.GetValue());
			else if (Baseline->BaselineType == 6)
				SWGTangibleBaselineParser::ParseBase6(Sub, Record.Tangible.GetValue());
		}
		else if (FourCC == TEXT("CREO"))
		{
			if (!Record.Creature.IsSet())
				Record.Creature = FCreatureObjectBaseline();

			FCreatureObjectBaseline& Creature = Record.Creature.GetValue();
			switch (Baseline->BaselineType)
			{
				case 1: SWGCreatureBaselineParser::ParseBase1(Sub, Creature); break;
				case 3: SWGCreatureBaselineParser::ParseBase3(Sub, Creature); break;
				case 4: SWGCreatureBaselineParser::ParseBase4(Sub, Creature); break;
				case 6: SWGCreatureBaselineParser::ParseBase6(Sub, Creature); break;
				default: break;
			}
		}
		else if (FourCC == TEXT("PLAY"))
		{
			if (!Record.Player.IsSet())
				Record.Player = FPlayerObjectBaseline();

			FPlayerObjectBaseline& Player = Record.Player.GetValue();
			switch (Baseline->BaselineType)
			{
				case 3: SWGPlayerBaselineParser::ParseBase3(Sub, Player); break;
				case 6: SWGPlayerBaselineParser::ParseBase6(Sub, Player); break;
				case 8: SWGPlayerBaselineParser::ParseBase8(Sub, Player); break;
				case 9: SWGPlayerBaselineParser::ParseBase9(Sub, Player); break;
				default: break;
			}
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("SWGObjectStoreSubsystem: no sub-object decoder for baseline type '%s' slot %d"),
				*FourCC, Baseline->BaselineType);
		}
	}
	else if (Opcode == static_cast<uint32>(ESWGMessageOp::SceneEndBaselines))
	{
		const auto* End = static_cast<const FSceneEndBaselinesMessage*>(Msg.Get());

		if (TSharedPtr<FSWGObjectRecord>* Found = Objects.Find(End->ObjectId))
		{
			(*Found)->bReady = true;
			OnObjectReady.Broadcast(End->ObjectId);
		}
	}
	else if (Opcode == static_cast<uint32>(ESWGMessageOp::DeltasMessage))
	{
		const auto* Delta = static_cast<const FDeltasMessage*>(Msg.Get());

		// Envelope + object type/slot are decoded (see FDeltasMessage); applying
		// individual field updates onto FTangibleObjectBaseline/FCreatureObjectBaseline/
		// FPlayerObjectBaseline requires a per-field-index switch mirroring the
		// baseline parsers above and is not yet wired in — RawUpdates holds the
		// opaque [fieldIndex][value] stream for future per-type apply functions.
		UE_LOG(LogTemp, Verbose, TEXT("SWGObjectStoreSubsystem: delta for object %lld type '%s' slot %d (%d update ops, not yet applied)"),
			Delta->ObjectId, *Delta->GetObjectTypeFourCC(), Delta->DeltaType, Delta->UpdateCount);
	}
}
