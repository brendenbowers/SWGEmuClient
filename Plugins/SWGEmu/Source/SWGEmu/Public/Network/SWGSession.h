#pragma once

#include "CoreMinimal.h"
#include "SWGPacket.h"

/**
 * ESWGSessionState tracks the handshake and connection lifecycle.
 */
enum class ESWGSessionState : uint8
{
	Disconnected,
	Connecting,  // SessionRequest sent, awaiting SessionResponse
	Connected,   // Handshake complete, encryption active
	Error        // Unrecoverable error
};

/**
 * FSWGSession holds the shared SOE connection parameters and inter-thread
 * message queues. All protocol sequencing/fragment/window state has been
 * moved into FSWGReliabilityComponent; only the facts that multiple components
 * need to read are kept here.
 *
 * Thread safety:
 *   OutgoingReliable / OutgoingUnreliable — written by game thread (SendMessage),
 *     written by reader-thread components (e.g. DataAck), drained by game thread Tick.
 *   IncomingMessages — written by reader thread (ReliabilityComponent), drained by
 *     game thread Tick (ProcessIncomingMessages).
 */
struct FSWGSession
{
	// ── Connection parameters (set by HandshakeComponent on SessionResponse) ──
	uint32 ConnectionID    = 0;
	uint32 EncryptionKey   = 0;
	uint32 MaxPacketSize   = 496;
	uint32 WindowResendSize = 32;

	ESWGSessionState State = ESWGSessionState::Disconnected;

	// ── Inter-thread queues ───────────────────────────────────────────────────
	TQueue<FSWGPacket, EQueueMode::Mpsc> OutgoingReliable;   // DataChannel1 send queue
	TQueue<FSWGPacket, EQueueMode::Mpsc> OutgoingUnreliable; // Unreliable / control packets
	TQueue<FSWGPacket, EQueueMode::Mpsc> IncomingMessages;   // Fully reassembled game messages

	// ── Ping / timeout tracking ───────────────────────────────────────────────
	uint64 LastPingReceived   = 0;
	uint64 LastPacketReceived = 0;
	uint64 LastPacketSent     = 0;

	bool IsConnected() const { return State == ESWGSessionState::Connected; }

	void Reset();
};
