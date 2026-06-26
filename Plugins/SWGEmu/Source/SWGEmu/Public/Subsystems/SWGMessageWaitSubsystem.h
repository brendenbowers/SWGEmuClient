#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Async/Future.h"
#include "Common/ResultTypes.h"
#include "Network/Messages/SWGNetMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "SWGMessageWaitSubsystem.generated.h"

class USWGNetworkSubsystem;
struct FSWGPacket;

using FSWGNetMessageResult = TResult<TSharedPtr<FSWGNetMessage>>;

/**
 * USWGMessageWaitSubsystem — request/response layer over the SOE message stream.
 *
 * Decoupled from USWGNetworkSubsystem: it subscribes to that subsystem's
 * OnMessageReceived broadcast and resolves pending waiters when a matching
 * opcode arrives. The network subsystem stays protocol-only; this subsystem
 * provides the "send a message and await the reply" semantics that login/zone
 * task sequences build on.
 *
 * Because OnMessageReceived now hands out TSharedPtr<FSWGNetMessage>, waiters
 * retain a shared reference — the message survives until every future that
 * wanted it has resolved. No copying, no dangling pointers.
 *
 * Usage (C++):
 *   auto* Wait = GetGameInstance()->GetSubsystem<USWGMessageWaitSubsystem>();
 *   Wait->SendAndWaitFor<FEnumerateCharacterIdMessage>(
 *           Req.Serialize(), ESWGMessageOp::EnumerateCharacterId)
 *       .Next([](TResult<TSharedPtr<const FEnumerateCharacterIdMessage>> R)
 *       {
 *           if (R.IsSuccess()) { ... R.GetValue()->Characters ... }
 *       });
 */
UCLASS()
class SWGEMU_API USWGMessageWaitSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject interface — drives waiter timeout expiry.
	virtual void    Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool    IsTickable() const override { return PendingWaiters.Num() > 0; }
	virtual bool    IsTickableWhenPaused() const override { return true; }

	// ── Primitives ────────────────────────────────────────────────

	/**
	 * Resolve when a message with this opcode arrives, or fail after TimeoutSeconds.
	 * Returns a typed shared view onto the message — safe to retain across frames.
	 *
	 * @param Opcode           Message opcode to wait for.
	 * @param TimeoutSeconds   Timeout duration; after this, the waiter fails.
	 * @param Predicate        Optional filter; if provided, only messages matching Predicate(Msg) resolve the waiter.
	 */
	template<typename T>
	TFuture<TResult<TSharedPtr<const T>>> WaitForMessage(
		ESWGMessageOp Opcode,
		float TimeoutSeconds = 10.f,
		TFunction<bool(const FSWGNetMessage&)> Predicate = nullptr)
	{
		// Adapt the base-type result into a typed shared pointer. Static cast is
		// safe: the opcode guarantees the concrete type registered for it.
		return WaitForMessageRaw(static_cast<uint32>(Opcode), TimeoutSeconds, MoveTemp(Predicate))
			.Next([](FSWGNetMessageResult Raw) -> TResult<TSharedPtr<const T>>
			{
				if (Raw.IsFailure())
					return TResult<TSharedPtr<const T>>::Failure(Raw.GetError());

				return TResult<TSharedPtr<const T>>::Success(
					StaticCastSharedPtr<const T>(Raw.GetValue()));
			});
	}

	/** Send a packet through the network subsystem, then await the given opcode. */
	template<typename T>
	TFuture<TResult<TSharedPtr<const T>>> SendAndWaitFor(
		const FSWGPacket& Packet,
		ESWGMessageOp Opcode,
		float TimeoutSeconds = 10.f,
		TFunction<bool(const FSWGNetMessage&)> Predicate = nullptr)
	{
		// Register the waiter BEFORE sending so a fast reply can't race past us.
		TFuture<TResult<TSharedPtr<const T>>> Future = WaitForMessage<T>(Opcode, TimeoutSeconds, Predicate);
		SendPacket(Packet);
		return Future;
	}

	/** Wait for multiple messages (one per opcode) to arrive before resolving. */
	TFuture<TResult<TMap<uint32, TSharedPtr<FSWGNetMessage>>>> WaitForAll(
		const TSet<uint32>& Opcodes,
		float TimeoutSeconds = 10.f);

	/** Fail every pending waiter with the given reason (e.g. on disconnect). */
	void CancelAll(const FString& Reason);

private:
	/** Bound to USWGNetworkSubsystem::OnMessageReceived. */
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Msg);

	/** Bound to USWGNetworkSubsystem::OnDisconnected; cancels all pending waiters. */
	void HandleDisconnected();

	/** Forward a packet to the network subsystem (reliable). */
	void SendPacket(const FSWGPacket& Packet);

	/**
	 * Core single-waiter primitive. Registers a pending waiter for the given opcode
	 * and returns a future on the base message type. Everything else — typed waits,
	 * multi-waits — composes over this.
	 */
	TFuture<FSWGNetMessageResult> WaitForMessageRaw(
		uint32 Opcode,
		float TimeoutSeconds,
		TFunction<bool(const FSWGNetMessage&)> Predicate);

	struct FPendingWaiter
	{
		uint32 Opcode = 0;
		double DeadlineSeconds = 0.0;
		TFunction<bool(const FSWGNetMessage&)> Predicate;
		TPromise<TResult<TSharedPtr<FSWGNetMessage>>> Promise;
	};

	TArray<FPendingWaiter> PendingWaiters;

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network = nullptr;

	FDelegateHandle MessageHandle;
	FDelegateHandle DisconnectHandle;
};
