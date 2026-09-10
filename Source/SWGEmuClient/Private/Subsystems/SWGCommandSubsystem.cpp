#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/ObjControllerMessageIn.h"
#include "Network/Messages/Zone/Object/CommandQueueEnqueue.h"
#include "Network/Messages/Zone/Object/CommandQueueRemoveIn.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "TRE/SWGCrc32.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGIffReader.h"

void USWGCommandSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Network = Cast<USWGNetworkSubsystem>(Collection.InitializeDependency(USWGNetworkSubsystem::StaticClass()));
	ObjectGraph = Cast<USWGObjectGraphSubsystem>(Collection.InitializeDependency(USWGObjectGraphSubsystem::StaticClass()));
	Tre = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));

	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGCommandSubsystem::HandleMessageReceived);
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().SetTimer(QueueTickHandle, FTimerDelegate::CreateUObject(this, &USWGCommandSubsystem::TickQueue),
			QueueTickInterval, /*bLoop*/ true);
	}
}

void USWGCommandSubsystem::Deinitialize()
{
	if (Network && MessageHandle.IsValid())
	{
		Network->OnMessageReceived.Remove(MessageHandle);
		MessageHandle.Reset();
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().ClearTimer(QueueTickHandle);
	}

	PendingCommands.Reset();

	Network = nullptr;
	ObjectGraph = nullptr;
	Tre = nullptr;
	NextActionCount = 1;

	KnownCommandNames.Reset();
	bCommandTableLoadAttempted = false;
}

void USWGCommandSubsystem::EnsureCommandTableLoaded() const
{
	if (bCommandTableLoadAttempted)
	{
		return;
	}

	bCommandTableLoadAttempted = true;

	if (!Tre)
	{
		return;
	}

	FSWGDataTableData Table;
	if (!FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/command/command_table.iff")), Table))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGCommandSubsystem: could not read the command table — command names will not be validated"));
		return;
	}

	const int32 NameColumn = Table.GetColumnIndex(TEXT("commandName"));
	if (NameColumn == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGCommandSubsystem: the command table has no commandName column"));
		return;
	}

	KnownCommandNames.Reserve(Table.Rows.Num());
	for (const FSWGDataTableRow& Row : Table.Rows)
	{
		if (Row.Cells.IsValidIndex(NameColumn) && !Row.Cells[NameColumn].IsEmpty())
		{
			// Lowercased to match how commands are hashed, which is also how
			// Core3 keys them.
			KnownCommandNames.Add(Row.Cells[NameColumn].ToLower());
		}
	}

	UE_LOG(LogTemp, Log, TEXT("USWGCommandSubsystem: %d commands known from the command table"), KnownCommandNames.Num());
}

bool USWGCommandSubsystem::IsKnownCommand(const FString& CommandName) const
{
	// Skill markers have command-table rows but no registered command class
	// server-side, so queuing one returns "Invalid enqueueCommand call".
	if (CommandName.StartsWith(TEXT("private_")))
	{
		return false;
	}

	EnsureCommandTableLoaded();

	// An unreadable table means we can't tell; assume yes rather than block everything.
	return KnownCommandNames.IsEmpty() || KnownCommandNames.Contains(CommandName.ToLower());
}

FString USWGCommandSubsystem::FindCommandName(uint32 ActionCount) const
{
	const FString* Found = RecentCommandNames.Find(ActionCount);
	return Found ? *Found : FString();
}

int64 USWGCommandSubsystem::HashCommandName(const FString& CommandName)
{
	return static_cast<int64>(FSWGCrc32::HashString(CommandName.ToLower()));
}

int32 USWGCommandSubsystem::SendCommand(const FString& CommandName, int64 TargetId, const FString& Arguments)
{
	const int64 PlayerObjectId = ObjectGraph ? ObjectGraph->GetLocalPlayerObjectId() : 0;
	if (!Network || PlayerObjectId == 0 || CommandName.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGCommandSubsystem: can't send command '%s' — no network or no local player yet"), *CommandName);
		return 0;
	}

	// Warn rather than refuse: a server may add commands our table lacks. The
	// server's own rejection is only a log line on its side, never a reply.
	if (!IsKnownCommand(CommandName))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGCommandSubsystem: '%s' is in no command table — the server will reject it as an invalid enqueueCommand"), *CommandName);
	}

	const uint32 ActionCRC = static_cast<uint32>(HashCommandName(CommandName));
	const uint32 ActionCount = NextActionCount++;

	FCommandQueueEnqueue Command(static_cast<uint64>(PlayerObjectId), ActionCRC, ActionCount, static_cast<uint64>(TargetId), Arguments);
	Network->SendMessage(Command.Serialize());

	UE_LOG(LogTemp, Log, TEXT("USWGCommandSubsystem: sent command '%s' (crc %08X, count %u, target %lld)"),
		*CommandName, ActionCRC, ActionCount, TargetId);

	RecentCommandNames.Add(ActionCount, CommandName);
	if (ActionCount > RecentCommandHistory)
	{
		// Counts only ever increase, so anything below the window is stale.
		const uint32 Oldest = ActionCount - RecentCommandHistory;
		for (auto It = RecentCommandNames.CreateIterator(); It; ++It)
		{
			if (It.Key() < Oldest)
			{
				It.RemoveCurrent();
			}
		}
	}

	FSWGQueuedCommand& Queued = PendingCommands.AddDefaulted_GetRef();
	Queued.CommandName = CommandName;
	Queued.ActionCount = static_cast<int32>(ActionCount);
	Queued.TargetId = TargetId;
	Queued.Arguments = Arguments;
	Queued.SentTime = FPlatformTime::Seconds();

	OnCommandQueueChanged.Broadcast();
	OnCommandSent.Broadcast(CommandName, ActionCount);

	return static_cast<int32>(ActionCount);
}

// ── Queue tracking ───────────────────────────────────────────────────────────

void USWGCommandSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Msg)
{
	if (!Msg || Msg->Opcode != static_cast<uint32>(ESWGMessageOp::ObjControllerMessage))
	{
		return;
	}

	const FObjControllerMessageIn& Envelope = *static_cast<const FObjControllerMessageIn*>(Msg.Get());
	if (Envelope.GetSubOp() != ESWGObjControllerOp::CommandQueueRemove)
	{
		return;
	}

	FSWGPacket Payload = Envelope.AsPayloadPacket();
	FCommandQueueRemoveIn Reply;
	if (Reply.Parse(Payload))
	{
		RetireCommand(Reply.ActionCount);
	}
}

void USWGCommandSubsystem::RetireCommand(uint32 ActionCount)
{
	// Counts only increase, so anything at or below the answered one is done —
	// a reply we never saw for an earlier command would otherwise strand it.
	const int32 Removed = PendingCommands.RemoveAll([ActionCount](const FSWGQueuedCommand& Entry)
	{
		return static_cast<uint32>(Entry.ActionCount) <= ActionCount;
	});

	if (Removed > 0)
	{
		OnCommandQueueChanged.Broadcast();
	}
}

void USWGCommandSubsystem::TickQueue()
{
	if (PendingCommands.IsEmpty())
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const int32 Expired = PendingCommands.RemoveAll([Now](const FSWGQueuedCommand& Entry)
	{
		return Now - Entry.SentTime > PendingCommandTimeout;
	});

	for (FSWGQueuedCommand& Entry : PendingCommands)
	{
		Entry.ElapsedSeconds = static_cast<float>(Now - Entry.SentTime);
	}

	if (Expired > 0)
	{
		OnCommandQueueChanged.Broadcast();
	}
}
