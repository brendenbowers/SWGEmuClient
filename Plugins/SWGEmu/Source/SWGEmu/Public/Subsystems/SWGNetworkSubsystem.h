#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Network/SWGSession.h"
#include "Network/SWGSocketReader.h"
#include "Network/SWGSocketWriter.h"
#include "Network/Messages/SWGMessage.h"
#include "SWGNetworkSubsystem.generated.h"

class FSocket;
class FRunnableThread;

/**
 * USWGNetworkSubsystem is the main entry point for the SWG Emu networking plugin.
 *
 * Responsibilities:
 * - Manage UDP socket lifecycle (create, connect, disconnect)
 * - Own the SWG session state and reliability tracking
 * - Spawn and manage socket reader/writer background threads
 * - Dispatch incoming messages to registered handlers
 * - Provide C++ / Blueprint APIs for sending messages
 *
 * This is a GameInstanceSubsystem, so it persists across level loads
 * and is tied to the game instance lifetime.
 *
 * Usage:
 *   USWGNetworkSubsystem* Net = GetGameInstance()->GetSubsystem<USWGNetworkSubsystem>();
 *   Net->Connect("loginserver.swgemu.com", 44453);
 */
UCLASS()
class SWGEMU_API USWGNetworkSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	USWGNetworkSubsystem();
	// Defined in the .cpp so TUniquePtr members can see the complete
	// FSWGSocketReader / FSWGSocketWriter types when destroyed.
	virtual ~USWGNetworkSubsystem() override;

	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	// ── Connection Lifecycle ──────────────────────────────────
	/**
	 * Establish a UDP connection to the server.
	 *
	 * This performs the SOE handshake (SessionRequest → SessionResponse).
	 * Returns when connected or an error occurs.
	 *
	 * @param HostAddress    Server IP or hostname (e.g., "loginserver.swgemu.com")
	 * @param Port           Server port (e.g., 44453 for login, 44463 for zone)
	 * @param OutError       [optional] Set to error message on failure
	 * @return               True if connected, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Network")
	bool Connect(const FString& HostAddress, int32 Port, FString& OutError);

	/**
	 * Cleanly disconnect from the server (sends Disconnect packet).
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Network")
	void Disconnect();

	/**
	 * Check if currently connected to the server.
	 */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Network")
	bool IsConnected() const;

	// ── Message Handlers ──────────────────────────────────────
	/**
	 * Delegate fired when a message is received and dispatched.
	 * C++ code can bind to this to listen for all messages.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMessageReceived, const FSWGMessage&);
	FOnMessageReceived OnMessageReceived;

	/**
	 * Register a handler for a specific message opcode.
	 *
	 * @param Opcode  The message opcode (ESWGMessageOp)
	 * @param Handler Callback function
	 */
	using FMessageHandler = TFunction<void(const FSWGMessage&)>;
	void RegisterMessageHandler(uint32 Opcode, FMessageHandler&& Handler);

	// ── Outgoing Messages ─────────────────────────────────────
	/**
	 * Queue a game message for transmission.
	 *
	 * @param Message     The message packet (opcode should be set)
	 * @param bReliable   If true, use DataChannel1 (with sequence/ACK); false for unreliable
	 */
	void SendMessage(const FSWGPacket& Message, bool bReliable = true);

	// ── Session State ─────────────────────────────────────────
	/**
	 * Get the current session state (for debugging/UI).
	 */
	ESWGSessionState GetSessionState() const { return Session.State; }

	/**
	 * Get the encryption key (used by crypto functions).
	 */
	uint32 GetEncryptionKey() const { return Session.EncryptionKey; }

private:
	// Socket and networking
	FSocket* Socket = nullptr;

	// Session state (shared with reader/writer threads)
	FSWGSession Session;

	// Background threads
	TUniquePtr<FSWGSocketReader> ReaderRunnable;
	TUniquePtr<FSWGSocketWriter> WriterRunnable;
	FRunnableThread* ReaderThread = nullptr;
	FRunnableThread* WriterThread = nullptr;

	// Message dispatch table
	TMap<uint32, FMessageHandler> MessageHandlers;

	// ── Internal Methods ──────────────────────────────────────
	/**
	 * Process all messages in the incoming queue and dispatch to handlers.
	 * Called from Tick.
	 */
	void ProcessIncomingMessages();

	/**
	 * Perform SOE SessionRequest/SessionResponse handshake.
	 * Called during Connect.
	 */
	bool PerformHandshake();

	/**
	 * Spawn the background reader and writer threads.
	 */
	void StartIOThreads();

	/**
	 * Stop and clean up the background threads.
	 */
	void StopIOThreads();
};
