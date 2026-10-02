#include "Subsystems/SWGIntangibleObjectSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGItemTransferSubsystem.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Objects/Zone/Intangible/IntangibleObjectBaseline.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"


void USWGIntangibleObjectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Network = Collection.InitializeDependency<USWGNetworkSubsystem>();
	Tre = Collection.InitializeDependency<USWGTreSubsystem>();

	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGIntangibleObjectSubsystem::HandleMessageReceived);
	}
}

void USWGIntangibleObjectSubsystem::Deinitialize()
{
	if (Network)
	{
		Network->OnMessageReceived.Remove(MessageHandle);
	}
	Super::Deinitialize();
}

void USWGIntangibleObjectSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message)
{
	if (!Message || static_cast<ESWGMessageOp>(Message->Opcode) != ESWGMessageOp::BaselinesMessage)
	{
		return;
	}

	const FBaselinesMessage& Baselines = *static_cast<const FBaselinesMessage*>(Message.Get());
	if (Baselines.GetObjectType() != ESWGObjectType::ITNO)
	{
		return;
	}

	// Diagnostic: confirms ITNO baselines actually arrive, and at what slot —
	// remove once name resolution is confirmed working end to end.
	UE_LOG(LogTemp, Warning, TEXT("USWGIntangibleObjectSubsystem: ITNO baseline object=%lld type=%d payloadBytes=%d"),
		Baselines.ObjectId, Baselines.BaselineType, Baselines.RawPayload.Num());

	if (Baselines.BaselineType != 3)
	{
		return;
	}

	FSWGPacket Packet = Baselines.AsPayloadPacket();
	FIntangibleObjectBaseline Baseline;
	SWGIntangibleBaselineParser::ParseBase3(Packet, Baseline);

	UE_LOG(LogTemp, Warning, TEXT("USWGIntangibleObjectSubsystem: parsed base3 object=%lld version=%.2f nameFile='%s' nameId='%s' customName='%s' volume=%d bytesLeft=%d"),
		Baselines.ObjectId, Baseline.UnknownVersion, *Baseline.ObjectName.File, *Baseline.ObjectName.StringTableId,
		*Baseline.CustomName, Baseline.Volume, Packet.GetRemaining());

	FSWGIntangibleEntry& Entry = Entries.FindOrAdd(Baselines.ObjectId);
	Entry.ObjectId = Baselines.ObjectId;

	if (!Baseline.CustomName.IsEmpty())
	{
		Entry.Name = Baseline.CustomName;
	}
	else if (Tre && !Baseline.ObjectName.StringTableId.IsEmpty())
	{
		const FString Resolved = Tre->LookupString(Baseline.ObjectName.File, Baseline.ObjectName.StringTableId);
		Entry.Name = Resolved.IsEmpty() ? Baseline.ObjectName.StringTableId : Resolved;
	}
	else
	{
		Entry.Name = Baseline.ObjectName.StringTableId;
	}

	UE_LOG(LogTemp, Warning, TEXT("USWGIntangibleObjectSubsystem: resolved name for %lld = '%s'"), Baselines.ObjectId, *Entry.Name);
}
