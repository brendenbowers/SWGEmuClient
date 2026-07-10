#pragma once

#include "CoreMinimal.h"
#include "PacketHandler.h"
#include "Net/Util/SequenceNumber.h"
#include "Network/SWGSessionOp.h"

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
	static FString GetComponentName();

	explicit FSWGReliabilityComponent(TWeakPtr<FSWGSession> InSession);

	// HandlerComponent interface
	virtual void Initialize() override;
	virtual bool IsValid() const override;
	virtual void Incoming(FBitReader& Packet) override;
	virtual void Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits) override;
	virtual void Tick(float DeltaTime) override;
	virtual int32 GetReservedPacketBits() const override;

	/**
	 * Set by FSWGPacketHandler to forward session ops this component doesn't
	 * itself handle (NetStatRequest, Disconnect, Ping...) to the Handshake
	 * component. Needed specifically for sub-packets unwrapped out of a
	 * MultiPacket (see HandleMultiPacket) — those only ever reach this
	 * component's own Incoming(), recursing into itself, and never reach the
	 * rest of the pipeline the way a top-level packet does via
	 * FSWGPacketHandler's own per-component loop. Without this, any such op
	 * bundled inside a MultiPacket was silently dropped here instead of ever
	 * reaching e.g. FSWGHandshakeComponent::HandleNetStatRequest.
	 */
	void SetUnhandledOpForwarder(TFunction<void(FBitReader&)> InForwarder) { ForwardUnhandledOp = MoveTemp(InForwarder); }

private:
	TWeakPtr<FSWGSession> SessionPtr;
	TFunction<void(FBitReader&)> ForwardUnhandledOp;

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

	// ── Out-of-order reorder buffer ───────────────────────────────────────────
	// DataChannel/DataFrag packets that arrived with seq > InSeqNext. Held here
	// instead of being discarded, so a single UDP reorder doesn't strand every
	// subsequent packet in a burst behind a sequence number that never advances
	// (each mismatch previously just re-requested the same gap and dropped the
	// packet in hand, so later arrivals kept mismatching against the same stuck
	// InSeqNext forever).
	struct FBufferedPacket
	{
		ESWGSessionOp Op = ESWGSessionOp::DataChannel1;
		TArray<uint8> Data;
	};
	TMap<uint16, FBufferedPacket> OutOfOrderBuffer;

	// ── Tick throttle ─────────────────────────────────────────────────────────
	double LastRetransmitCheckTime = 0.0;

	// ── Stall detection ───────────────────────────────────────────────────────
	// Core3's server-side DataOrder handler is dead code (BaseClient::resendPackets
	// unconditionally returns before its resend body) — a genuine gap in the
	// incoming sequence stream can never be filled by the request/response path
	// this component uses. Track repeated requests for the same stuck sequence so
	// that shows up as one unmistakable log line instead of silent traffic death.
	uint16 LastRequestedOrderSeq = 0;
	int32  OrderResendStreak = 0;

	// ── Helpers ───────────────────────────────────────────────────────────────

	void HandleDataAck(FBitReader& Packet);
	void HandleDataChannel(FBitReader& Packet);
	void HandleDataFrag(FBitReader& Packet);
	void HandleMultiPacket(FBitReader& Packet);
	void HandleRetransmits(float DeltaTime);
	void SendDataAck(uint16 Sequence);
	void SendDataOrder(uint16 Sequence);
	void UnbundleMessages(const uint8* Data, int32 Len);

	/** Process an in-order DataChannel payload (everything after the op/seq header). */
	void ProcessDataChannelPayload(const uint8* Data, int32 NumBytes);
	/** Process an in-order DataFrag payload (everything after the op/seq header). */
	void ProcessDataFragPayload(const uint8* Data, int32 NumBytes);
	/** After advancing InSeqNext, replay any buffered packets that are now next in line. */
	void DrainOutOfOrderBuffer();

	/** Consume and return the next outgoing sequence number (wraps at 0xFFFF). */
	uint16 GetNextOutSeq() { return (OutSeqNext++).Get(); }
};
