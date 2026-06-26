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

	// Collect expired single waiters.
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

	// Pull all matching single waiters out (FIFO) before resolving, to avoid re-entrancy
	// if a continuation immediately registers another waiter for the same opcode.
	// Unmatched predicates stay in queue to await a different message.
	TArray<TPromise<FSWGNetMessageResult>> ToFulfill;
	for (int32 i = 0; i < PendingWaiters.Num(); )
	{
		USWGMessageWaitSubsystem::FPendingWaiter& PendingWaiter = PendingWaiters[i];
		if (PendingWaiter.Opcode == Opcode)
		{
			// Check predicate if provided; if it fails, skip this waiter
			if (PendingWaiter.Predicate && !PendingWaiter.Predicate(*Msg))
			{
				++i;
				continue;
			}

			ToFulfill.Add(MoveTemp(PendingWaiter.Promise));
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

TFuture<FSWGNetMessageResult> USWGMessageWaitSubsystem::WaitForMessageRaw(
	uint32 Opcode,
	float TimeoutSeconds,
	TFunction<bool(const FSWGNetMessage&)> Predicate)
{
	FPendingWaiter Waiter;
	Waiter.Opcode          = Opcode;
	Waiter.DeadlineSeconds = FPlatformTime::Seconds() + TimeoutSeconds;
	Waiter.Predicate       = MoveTemp(Predicate);

	TFuture<FSWGNetMessageResult> Future = Waiter.Promise.GetFuture();
	PendingWaiters.Add(MoveTemp(Waiter));
	return Future;
}

TFuture<TResult<TMap<uint32, TSharedPtr<FSWGNetMessage>>>> USWGMessageWaitSubsystem::WaitForAll(
	const TSet<uint32>& Opcodes,
	float TimeoutSeconds)
{
	using FMapResult = TResult<TMap<uint32, TSharedPtr<FSWGNetMessage>>>;

	// Shared so the single move-only promise can be captured by every child waiter's
	// continuation. The future is handed back to the caller before any child resolves.
	TSharedRef<TPromise<FMapResult>> Promise = MakeShared<TPromise<FMapResult>>();
	TFuture<FMapResult> Future = Promise->GetFuture();

	if (Opcodes.IsEmpty())
	{
		Promise->SetValue(FMapResult::Success({}));
		return Future;
	}

	// A multi-wait is just N single waiters plus a shared accumulator. The first
	// failure (timeout/cancel) fails the group; the last success completes it.
	// bResolved guards against double-set once the group has settled either way.
	struct FAccumulator
	{
		TMap<uint32, TSharedPtr<FSWGNetMessage>> Messages;
		int32 Remaining = 0;
		bool  bResolved = false;
	};
	TSharedRef<FAccumulator> Acc = MakeShared<FAccumulator>();
	Acc->Remaining = Opcodes.Num();

	for (uint32 Opcode : Opcodes)
	{
		WaitForMessageRaw(Opcode, TimeoutSeconds, nullptr)
			.Next([Promise, Acc, Opcode](FSWGNetMessageResult Result)
			{
				if (Acc->bResolved)
					return;

				if (Result.IsFailure())
				{
					Acc->bResolved = true;
					Promise->SetValue(FMapResult::Failure(Result.GetError()));
					return;
				}

				Acc->Messages.Add(Opcode, Result.GetValue());
				if (--Acc->Remaining == 0)
				{
					Acc->bResolved = true;
					Promise->SetValue(FMapResult::Success(MoveTemp(Acc->Messages)));
				}
			});
	}

	return Future;
}

void USWGMessageWaitSubsystem::SendPacket(const FSWGPacket& Packet)
{
	if (Network)
		Network->SendMessage(Packet);
}

void USWGMessageWaitSubsystem::CancelAll(const FString& Reason)
{
	// Fail every pending waiter. Multi-waits ride on top of these single waiters,
	// so failing the children fails their groups too — no separate handling needed.
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
