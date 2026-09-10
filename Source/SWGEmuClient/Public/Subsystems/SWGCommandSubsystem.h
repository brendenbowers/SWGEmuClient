#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGCommandSubsystem.generated.h"

class USWGNetworkSubsystem;
class USWGObjectGraphSubsystem;
class USWGTreSubsystem;
struct FSWGNetMessage;

/**
 * A command we have sent that the server has not replied to yet.
 *
 * The queue proper lives server-side; this is our view of it — everything
 * enqueued and still unanswered, oldest first, which is the order Core3 runs
 * them in, so index 0 is the one executing.
 */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGQueuedCommand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Commands")
	FString CommandName;

	/** The sequence the server echoes back on its reply. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Commands")
	int32 ActionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Commands")
	int64 TargetId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Commands")
	FString Arguments;

	/** Seconds since it was sent — how long the server has been sitting on it. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Commands")
	float ElapsedSeconds = 0.f;

	/** When the send happened, in FPlatformTime::Seconds(). */
	double SentTime = 0.0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnCommandQueueChanged);

/**
 * Sends player commands to the server — the path behind ability use, toolbar
 * presses and slash commands.
 *
 * Commands are identified on the wire by the hash of their lowercased name,
 * which is how Core3 registers them.
 */
UCLASS()
class SWGEMUCLIENT_API USWGCommandSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Queues a command by name ("burstrun", "attack"). TargetId 0 means no
	 * target. Returns the action count it was sent under — the server echoes
	 * that back on its reply — or 0 if there was nothing to send through.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Commands")
	int32 SendCommand(const FString& CommandName, int64 TargetId = 0, const FString& Arguments = FString());

	/** Everything sent and not yet answered, oldest first. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Commands")
	const TArray<FSWGQueuedCommand>& GetQueuedCommands() const { return PendingCommands; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Commands")
	int32 GetQueueLength() const { return PendingCommands.Num(); }

	/** Fired whenever the pending list gains or loses an entry. */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Commands")
	FSWGOnCommandQueueChanged OnCommandQueueChanged;

	/** Fired after a command goes out, whatever sent it — toolbar, console or code. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FSWGOnCommandSent, const FString& /*CommandName*/, uint32 /*ActionCount*/);
	FSWGOnCommandSent OnCommandSent;

	/**
	 * The command sent under ActionCount, or empty once it has aged out.
	 * Lets a CommandQueueRemove reply be reported against the name that
	 * caused it — the reply itself carries only the count.
	 */
	FString FindCommandName(uint32 ActionCount) const;

	/** The CRC the server knows a command by. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Commands")
	static int64 HashCommandName(const FString& CommandName);

	/**
	 * True if CommandName is a queued command the server will accept — it has a
	 * command_table.iff row and isn't a "private_" skill marker.
	 *
	 * A player's ability list is not a command list, so anything filling a
	 * toolbar from it must filter first. Assumes true if the table won't load.
	 */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Commands")
	bool IsKnownCommand(const FString& CommandName) const;

private:
	/** Populates KnownCommandNames from the command table on first use. */
	void EnsureCommandTableLoaded() const;

	/** Watches for the CommandQueueRemove that retires a pending entry. */
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Msg);

	/** Drops the answered entry, and any older one the server skipped past. */
	void RetireCommand(uint32 ActionCount);

	/** Refreshes ElapsedSeconds and drops entries whose reply never came. */
	void TickQueue();

	FDelegateHandle MessageHandle;
	FTimerHandle QueueTickHandle;

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	/** Lowercased command names from the command table. Empty when the table couldn't be read. */
	mutable TSet<FString> KnownCommandNames;

	/** Set once the load has been attempted, so an unreadable table isn't re-read on every check. */
	mutable bool bCommandTableLoadAttempted = false;

	/** Per-command sequence echoed back on the server's reply. Starts at 1; 0 reads as "not sent". */
	uint32 NextActionCount = 1;

	/** ActionCount -> command name, trimmed to the most recent RecentCommandHistory sends. */
	TMap<uint32, FString> RecentCommandNames;

	static constexpr uint32 RecentCommandHistory = 64;

	/** Sent and unanswered, oldest first. */
	TArray<FSWGQueuedCommand> PendingCommands;

	/**
	 * How long an unanswered command stays listed. A command the server
	 * refuses outright gets no reply at all, so an entry that ages out is
	 * assumed gone rather than left on screen forever.
	 */
	static constexpr float PendingCommandTimeout = 15.f;

	/** How often ElapsedSeconds is refreshed and timeouts are checked. */
	static constexpr float QueueTickInterval = 0.25f;
};
