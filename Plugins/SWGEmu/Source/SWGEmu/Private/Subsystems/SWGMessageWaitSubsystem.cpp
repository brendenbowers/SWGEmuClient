#include "Subsystems/SWGMessageWaitSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Network/SWGPacket.h"

void USWGMessageWaitSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Force the network subsystem to initialize first, then subscribe to its
	// message broadcast and disconnect event. Keeps this subsystem fully decoupled from transport.
	Network = Cast<USWGNetworkSubsystem>(
		Collection.InitializeDependency(USWGNetworkSubsystem::StaticClass()));

	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(
			this, &USWGMessageWaitSubsystem::HandleMessageReceived);
		DisconnectHandle = Network->OnDisconnected.AddUObject(
			this, &USWGMessageWaitSubsystem::HandleDisconnected);
	}

	UE_LOG(LogTemp, Log, TEXT("SWGMessageWaitSubsystem: initialized"));
}

void USWGMessageWaitSubsystem::Deinitialize()
{
	if (Network)
	{
		if (MessageHandle.IsValid())
		{
			Network->OnMessageReceived.Remove(MessageHandle);
			MessageHandle.Reset();
		}
		if (DisconnectHandle.IsValid())
		{
			Network->OnDisconnected.Remove(DisconnectHandle);
			DisconnectHandle.Reset();
		}
	}

	CancelAll(TEXT("Wait subsystem shutting down"));

	Super::Deinitialize();
	UE_LOG(LogTemp, Log, TEXT("SWGMessageWaitSubsystem: deinitialized"));
}

// ── Tick (timeout expiry) ──────────────────────────────────────────────────────

void USWGMessageWaitSubsystem::Tick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();

	// Collect expired promises first, then resolve them outside the loop so a
	// continuation that registers a new waiter can't disturb our iteration.
	TArray<TPromise<FSWGNetMessageResult>> Expired;
	for (int32 i = 0; i < PendingWaiters.Num(); )
	{
		if (Now >= PendingWaiters[i].DeadlineSeconds)
		{
			Expired.Add(MoveTemp(PendingWaiters[i].Promise));
			PendingWaiters.RemoveAt(i);
		}
		else
		{
			++i;
		}
	}

	for (TPromise<FSWGNetMessageResult>& P : Expired)
	{
		P.SetValue(FSWGNetMessageResult::Failure(
			TEXT("Timed out waiting for response")));
	}
}

TStatId USWGMessageWaitSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGMessageWaitSubsystem, STATGROUP_Tickables);
}

// ── Dispatch ───────────────────────────────────────────────────────────────────

void USWGMessageWaitSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Msg)
{
	if (!Msg.IsValid())
		return;

	const uint32 Opcode = Msg->Opcode;

	// Pull all matching waiters out (FIFO) before resolving, to avoid re-entrancy
	// if a continuation immediately registers another waiter for the same opcode.
	// Unmatched predicates stay in queue to await a different message.
	TArray<TPromise<FSWGNetMessageResult>> ToFulfill;
	for (int32 i = 0; i < PendingWaiters.Num(); )
	{
		if (PendingWaiters[i].Opcode == Opcode)
		{
			// Check predicate if provided; if it fails, skip this waiter
			if (PendingWaiters[i].Predicate && !PendingWaiters[i].Predicate(*Msg))
			{
				++i;
				continue;
			}

			ToFulfill.Add(MoveTemp(PendingWaiters[i].Promise));
			PendingWaiters.RemoveAt(i);
		}
		else
		{
			++i;
		}
	}

	for (TPromise<FSWGNetMessageResult>& P : ToFulfill)
	{
		P.SetValue(FSWGNetMessageResult::Success(Msg));
	}
}

void USWGMessageWaitSubsystem::SendPacket(const FSWGPacket& Packet)
{
	if (Network)
		Network->SendMessage(Packet);
}

void USWGMessageWaitSubsystem::CancelAll(const FString& Reason)
{
	TArray<TPromise<FSWGNetMessageResult>> All;
	All.Reserve(PendingWaiters.Num());
	for (FPendingWaiter& W : PendingWaiters)
		All.Add(MoveTemp(W.Promise));
	PendingWaiters.Reset();

	for (TPromise<FSWGNetMessageResult>& P : All)
	{
		P.SetValue(FSWGNetMessageResult::Failure(Reason));
	}
}

void USWGMessageWaitSubsystem::HandleDisconnected()
{
	CancelAll(TEXT("Network disconnected"));
}
