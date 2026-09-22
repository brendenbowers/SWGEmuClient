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

DEFINE_LOG_CATEGORY_STATIC(LogSWGDatapadDump, Log, All);

// swg.DumpDatapad — logs exactly what the client knows about the local
// player's datapad bag and each of its contents (object id, crc, resolved
// template path, whether an actor spawned, whether an ITNO baseline/name
// has arrived) — a one-shot alternative to grepping the raw session log.
static FAutoConsoleCommandWithWorldAndArgs GSWGDumpDatapadCommand(
	TEXT("swg.DumpDatapad"),
	TEXT("Logs the local player's datapad bag id and every contained object's id/crc/template/name state."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (GEngine && (!World || !World->IsGameWorld()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
					break;
				}
			}
		}

		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
		USWGItemTransferSubsystem* Transfer = GameInstance ? GameInstance->GetSubsystem<USWGItemTransferSubsystem>() : nullptr;
		USWGIntangibleObjectSubsystem* Intangibles = GameInstance ? GameInstance->GetSubsystem<USWGIntangibleObjectSubsystem>() : nullptr;
		USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
		if (!ObjectGraph || !Transfer)
		{
			UE_LOG(LogSWGDatapadDump, Warning, TEXT("swg.DumpDatapad: no live session yet"));
			return;
		}

		const int64 BagId = Transfer->FindDatapadBagId();
		UE_LOG(LogSWGDatapadDump, Log, TEXT("swg.DumpDatapad: bag id = %lld"), BagId);
		if (BagId == 0)
		{
			return;
		}

		const TArray<int64> Contents = ObjectGraph->FindContainedObjectIds(BagId);
		UE_LOG(LogSWGDatapadDump, Log, TEXT("swg.DumpDatapad: %d content object(s)"), Contents.Num());
		for (const int64 ObjectId : Contents)
		{
			AActor* Actor = ObjectGraph->FindActor(ObjectId);
			const uint32 Crc = ObjectGraph->FindObjectCrc(ObjectId);
			const FString TemplatePath = Tre && Crc != 0 ? Tre->ResolveTemplatePath(Crc) : FString();
			const FSWGIntangibleEntry* Entry = Intangibles ? Intangibles->FindEntry(ObjectId) : nullptr;

			UE_LOG(LogSWGDatapadDump, Log, TEXT("  %lld: crc=%08X template='%s' actor=%s netObj=%s intangibleEntry=%s name='%s'"),
				ObjectId, Crc, *TemplatePath,
				Actor ? *Actor->GetClass()->GetName() : TEXT("<none>"),
				(Actor && Cast<ISWGNetworkObjectInterface>(Actor)) ? TEXT("yes") : TEXT("no"),
				Entry ? TEXT("yes") : TEXT("no"),
				Entry ? *Entry->Name : TEXT(""));
		}
	}));

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
