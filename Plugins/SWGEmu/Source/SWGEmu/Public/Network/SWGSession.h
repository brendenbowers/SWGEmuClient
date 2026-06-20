#pragma once

#include "CoreMinimal.h"
#include "Net/Util/SequenceNumber.h"
#include "SWGPacket.h"

/** 16-bit SOE sequence number with correct modular wrap-around arithmetic. */
using FSWGSeqNum = TSequenceNumber<16, uint16>;

/**
 * ESWGSessionState tracks the handshake and connection lifecycle.
 */
enum class ESWGSessionState : uint8
{
	Disconnected,  // Not connected
	Connecting,    // SessionRequest sent, awaiting SessionResponse
	Connected,     // Handshake complete, encryption active
	Error          // Unrecoverable error (timeout, bad handshake)
};

/**
 * FSWGSession manages the SOE session state: encryption key, sequence numbers,
 * reliability window, and packet reassembly buffers.
 *
 * The session does not own the socket;
 * that is owned by the network subsystem.
 */
struct FSWGSession
{
	// Handshake / connection parameters
	uint32 EncryptionKey = 0;
	uint32 MaxPacketSize = 496;
	uint32 WindowResendSize = 32;

	ESWGSessionState State = ESWGSessionState::Disconnected;

	// Reliability: sequence numbers
	// TSequenceNumber handles 0xFFFF->0x0000 wrap-around via modular arithmetic,
	// fixing the rollover bug that plain uint16 comparisons have in HandleDataAck.
	FSWGSeqNum OutSeqNext; // Next outgoing sequence number to assign.
	FSWGSeqNum InSeqNext;  // Next incoming sequence number expected from the server.

	uint32 LastSeqAcked = 0;

	// Message queues (thread-safe, accessed by reader & writer threads)
	TQueue<FSWGPacket, EQueueMode::Mpsc> OutgoingReliable;   // DataChannel1 send queue
	TQueue<FSWGPacket, EQueueMode::Mpsc> OutgoingUnreliable; // Unreliable / control packets
	TQueue<FSWGPacket, EQueueMode::Mpsc> IncomingMessages;   // Fully reassembled game messages

	// Fragment reassembly state
	uint32 FragTotalSize   = 0;
	uint32 FragCurrentSize = 0;
	TArray<uint8> FragBuffer;

	// Reliability window: packets sent but not yet ACK'd. Protected by WindowLock.
	TArray<FSWGPacket> WindowPackets;
	FCriticalSection   WindowLock;

	// Ping / timeout tracking
	uint64 LastPingReceived  = 0;
	uint64 LastPacketReceived = 0;
	uint64 LastPacketSent     = 0;

	// Helpers
	bool IsConnected() const { return State == ESWGSessionState::Connected; }

	/** Consume the next outgoing sequence number (post-increment, wraps at 0xFFFF). */
	uint16 GetNextOutSeq() { return (OutSeqNext++).Get(); }

	void Reset();
};
