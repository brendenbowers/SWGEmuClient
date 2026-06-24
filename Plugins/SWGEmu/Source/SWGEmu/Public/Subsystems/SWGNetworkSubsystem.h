#pragma once

#include "CoreMinimal.h"
#include "Async/Async.h"
#include "Async/Future.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Common/ResultTypes.h"
#include "Network/SWGSession.h"
#include "Network/SWGSocketReader.h"
#include "Network/Handler/SWGPacketHandler.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGNetMessage.h"
#include "Network/Messages/SWGMessageRegistry.h"
#include "SWGNetworkSubsystem.generated.h"

class FSocket;
class FRunnableThread;

/**
 * USWGNetworkSubsystem — main entry point for the SWGEmu networking plugin.
 *
 * Owns the UDP socket, session state, and the FSWGPacketHandler pipeline.
 * A single background thread (FSWGSocketReader) receives datagrams and pushes
 * them through the pipeline. Outgoing packets are drained tick-driven from the
 * game thread (mirrors UIpNetDriver::TickFlush), so no writer thread is needed.
 *
 * Usage:
 *   USWGNetworkSubsystem* Net = GetGameInstance()->GetSubsystem<USWGNetworkSubsystem>();
 *   Net->Connect("loginserver.swgemu.com", 44453, Error);
 */
UCLASS()
class SWGEMU_API USWGNetworkSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	USWGNetworkSubsystem();
	// Out-of-line dtor so TUniquePtr<FSWGSocketReader> / TUniquePtr<FSWGPacketHandler>
	// see complete types at destruction.
	virtual ~USWGNetworkSubsystem() override;

	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	// ── Connection Lifecycle ──────────────────────────────────────

	/**
	 * Establish a UDP connection to the SOE server.
	 *
	 * Sends a SessionRequest, blocks until SessionResponse arrives (handshake
	 * complete) or the timeout expires.
	 *
	 * @param HostAddress  Server IP or hostname.
	 * @param Port         Server port (44453 login, 44463 zone).
	 * @param OutError     Set to an error description on failure.
	 * @return             True if handshake completed successfully.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Network")
	bool Connect(const FString& HostAddress, int32 Port, FString& OutError);

	/**
	 * Asynchronously establish a UDP connection to the SOE server (C++ only).
	 *
	 * Returns a TFuture that resolves to success/failure. Use .Next() to register
	 * a callback that fires on the game thread when the connection completes.
	 *
	 * @param HostAddress  Server IP or hostname.
	 * @param Port         Server port (44453 login, 44463 zone).
	 * @return             TFuture that resolves to TResult<void>.
	 */
	TFuture<TResult<void>> ConnectAsync(const FString& HostAddress, int32 Port);

	/** Send a SOE Disconnect packet and clean up resources. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Network")
	void Disconnect();

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Network")
	bool IsConnected() const;

	// ── Message Handling ──────────────────────────────────────────

	/** Fired on the game thread for every constructed typed message. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMessageReceived, TSharedPtr<FSWGNetMessage>);
	FOnMessageReceived OnMessageReceived;

	using FMessageHandler = TFunction<void(TSharedPtr<FSWGNetMessage>)>;

	/** Register a typed handler for a specific message opcode. The concrete type is cast internally. */
	void RegisterMessageHandler(uint32 Opcode, FMessageHandler&& Handler);

	/**
	 * Register a typed handler that receives the concrete message type directly.
	 *
	 * Example:
	 *   Net->RegisterMessageHandler<FLoginClusterStatusMessage>(
	 *       ESWGMessageOp::LoginClusterStatus,
	 *       [](TSharedPtr<const FLoginClusterStatusMessage> Msg) { ... });
	 */
	template<typename T>
	void RegisterMessageHandler(ESWGMessageOp Opcode, TFunction<void(TSharedPtr<const T>)> Handler)
	{
		RegisterMessageHandler(static_cast<uint32>(Opcode),
			[Handler](TSharedPtr<FSWGNetMessage> Msg)
			{
				Handler(StaticCastSharedPtr<const T>(Msg));
			});
	}

	// ── Outgoing ─────────────────────────────────────────────────

	/**
	 * Queue a game message for transmission.
	 *
	 * @param Message   Pre-built packet (caller sets op/payload).
	 * @param bReliable If true, uses DataChannel1 (sequence-tracked); false for unreliable.
	 */
	void SendMessage(const FSWGPacket& Message, bool bReliable = true);

	// ── Session Accessors ─────────────────────────────────────────

	ESWGSessionState GetSessionState() const { return Session.State; }
	uint32 GetEncryptionKey() const { return Session.EncryptionKey; }

private:
	FSocket*						Socket         = nullptr;
	FSWGSession						Session;
	TUniquePtr<FSWGPacketHandler>	PacketHandler;
	TUniquePtr<FSWGSocketReader>	ReaderRunnable;
	FRunnableThread*				ReaderThread   = nullptr;

	TMap<uint32, FMessageHandler>	MessageHandlers;

	// ── Internal ──────────────────────────────────────────────────

	/** Drain OutgoingUnreliable + OutgoingReliable, run each through the handler, send. */
	void FlushOutgoingQueues();

	/** Dequeue IncomingMessages and dispatch to registered handlers. */
	void ProcessIncomingMessages();

	/** Stop and join the reader thread; destroys the handler. */
	void StopIOThreads();

	/**
	 * Send the raw bytes of a pre-built FSWGPacket through the handler pipeline
	 * (Reliability → Compression → Encryption → CRC) and transmit via socket.
	 */
	void SendPacketThroughPipeline(const FSWGPacket& Pkt);
};
