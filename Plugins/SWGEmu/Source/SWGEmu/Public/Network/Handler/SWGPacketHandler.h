#pragma once

#include "CoreMinimal.h"
#include "PacketHandler.h"

struct FSWGSession;

/**
 * FSWGPacketHandler — thin owner of the ordered SOE HandlerComponent pipeline.
 *
 * Construction order IS the receive order:
 *   [ Crc, Encryption, Compression, Reliability, Handshake ]
 *
 * Incoming runs the list front-to-back; Outgoing runs it back-to-front.
 * A small custom owner is used instead of UE's stock PacketHandler because the
 * SOE pipeline is fixed and domain-specific.
 */
class FSWGPacketHandler
{
public:
	explicit FSWGPacketHandler(FSWGSession* InSession);
	~FSWGPacketHandler();

	/** Build the ordered component pipeline (Crc → Encryption → Compression → Reliability → Handshake). */
	void Initialize();

	/** Run all components' Incoming front-to-back. */
	void Incoming(FBitReader& Packet);

	/** Run all components' Outgoing back-to-front. */
	void Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits);

	/** Tick components that need it (e.g. reliability resends). */
	void Tick(float DeltaTime);

	/**
	 * Kick off the handshake: calls NotifyHandshakeBegin on any component that
	 * requires one (i.e. FSWGHandshakeComponent). Must be called after Initialize()
	 * and before the first Send/Recv. The handshake component queues a SessionRequest
	 * to Session->OutgoingUnreliable; flush that queue to the socket to start the
	 * SOE session exchange.
	 */
	void TriggerHandshake();

	/** Sum of reserved bits across components — validate against the 496-byte SOE MTU. */
	int32 GetTotalReservedPacketBits() const;

private:
	FSWGSession* Session;

	/** Ordered pipeline; index 0 is the first Incoming stage. */
	TArray<TSharedPtr<HandlerComponent>> Components;
};
