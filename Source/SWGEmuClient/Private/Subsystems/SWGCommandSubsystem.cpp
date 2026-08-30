#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Network/Messages/Zone/Object/CommandQueueEnqueue.h"
#include "TRE/SWGCrc32.h"

void USWGCommandSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Network = Cast<USWGNetworkSubsystem>(Collection.InitializeDependency(USWGNetworkSubsystem::StaticClass()));
	ObjectGraph = Cast<USWGObjectGraphSubsystem>(Collection.InitializeDependency(USWGObjectGraphSubsystem::StaticClass()));
}

void USWGCommandSubsystem::Deinitialize()
{
	Network = nullptr;
	ObjectGraph = nullptr;
	NextActionCount = 1;
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

	const uint32 ActionCRC = static_cast<uint32>(HashCommandName(CommandName));
	const uint32 ActionCount = NextActionCount++;

	FCommandQueueEnqueue Command(static_cast<uint64>(PlayerObjectId), ActionCRC, ActionCount, static_cast<uint64>(TargetId), Arguments);
	Network->SendMessage(Command.Serialize());

	UE_LOG(LogTemp, Log, TEXT("USWGCommandSubsystem: sent command '%s' (crc %08X, count %u, target %lld)"),
		*CommandName, ActionCRC, ActionCount, TargetId);

	return static_cast<int32>(ActionCount);
}
