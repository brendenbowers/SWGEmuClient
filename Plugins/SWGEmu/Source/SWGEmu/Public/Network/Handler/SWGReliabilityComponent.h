#pragma once

#include "CoreMinimal.h"
#include "PacketHandler.h"
#include "Net/Util/SequenceNumber.h"

struct FSWGSession;
struct FSWGPacket;

/** 16-bit SOE sequence number with correct modular wrap-around arithmetic. */
using FSWGSeqNum = TSequenceNumber<16, uint16>;

/**
 * FSWGReliabilityComponent — SOE reliability stage.
 *
 * Owns all sequence/ACK/window/fragment state so that FSWGSession only
 * needs to hold connection parameters and the inter-thread message queues.
 *
 * Outgoing: buffer DataChannel/DataFrag packets in the resend window.
 * Incoming: retire ACK'd packets, deliver in-order, reassemble fragments and
 *           multi-packets, then push finished payloads to Session.IncomingMessages.
 * Tick:     resend unacked window packets past the retransmit interval.
 */
class FSWGReliabilityComponent : public HandlerComponent
{
public:
	explicit FSWGReliabilityComponent(FSWGSession* InSession);

	// HandlerComponent interface
	virtual void Initialize() override;
	virtual bool IsValid() const override;
	virtual void Incoming(FBitReader& Packet) override;
	virtual void Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits) override;
	virtual void Tick(float DeltaTime) override;
	virtual int32 GetReservedPacketBits() const override;

private:
	FSWGSession* Session;

	// ── Sequence state (moved from FSWGSession) ───────────────────────────────
	FSWGSeqNum OutSeqNext;   // Next outgoing sequence number to assign
	FSWGSeqNum InSeqNext;    // Next incoming sequence number expected
	uint32     LastSeqAcked = 0;

	// ── Reliability window ────────────────────────────────────────────────────
	TArray<FSWGPacket> WindowPackets; // Sent but not yet ACK'd
	FCriticalSection   WindowLock;

	// ── Fragment reassembly ───────────────────────────────────────────────────
	uint32       FragTotalSize   = 0;
	uint32       FragCurrentSize = 0;
	TArray<uint8> FragBuffer;

	// ── Tick throttle ─────────────────────────────────────────────────────────
	double LastRetransmitCheckTime = 0.0;

	// ── Helpers ───────────────────────────────────────────────────────────────

	void HandleDataAck(FBitReader& Packet);
	void HandleDataChannel(FBitReader& Packet);
	void HandleDataFrag(FBitReader& Packet);
	void HandleMultiPacket(FBitReader& Packet);
	void HandleRetransmits(float DeltaTime);
	void SendDataAck(uint16 Sequence);
	void SendDataOrder(uint16 Sequence);
	void UnbundleMessages(const uint8* Data, int32 Len);

	/** Consume and return the next outgoing sequence number (wraps at 0xFFFF). */
	uint16 GetNextOutSeq() { return (OutSeqNext++).Get(); }
};
