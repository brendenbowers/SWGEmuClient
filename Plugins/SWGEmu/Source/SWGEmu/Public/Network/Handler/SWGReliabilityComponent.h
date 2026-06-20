#pragma once

#include "CoreMinimal.h"
#include "PacketHandler.h"

struct FSWGSession;
struct FSWGPacket;

/**
 * FSWGReliabilityComponent — SOE reliability stage (the big one).
 *
 * Owns the sequence/ACK window, fragment reassembly, and multi-packet unbundling.
 * Modeled on UE's ReliabilityHandlerComponent (sequence tracking, buffered-resend list,
 * and a resend timer driven from Tick()).
 *
 * Outgoing: buffer DataChannel/DataFrag packets for resend.
 * Incoming: retire ACK'd packets, deliver in-order, reassemble fragments and multi-packets,
 *           then push finished payloads to the session's IncomingMessages queue.
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

	// TODO: Phase 1 - move reliability state here from FSWGSession:
	//   OutSeqNext, InSeqNext, LastSeqAcked, WindowPackets, WindowLock,
	//   FragTotalSize, FragCurrentSize, FragBuffer, OutgoingReliable, IncomingMessages.

	/** Remove ACK'd packets from the resend window. */
	void HandleDataAck(FBitReader& Packet);

	/** Deliver an in-order DataChannel payload and emit a DataAck. */
	void HandleDataChannel(FBitReader& Packet);

	/** Accumulate a fragment; push the full message when complete. */
	void HandleDataFrag(FBitReader& Packet);

	/** Unbundle a multi-packet into its sub-packets. */
	void HandleMultiPacket(FBitReader& Packet);

	/** Resend unacked window packets past the resend interval (from Tick). */
	void HandleRetransmits(float DeltaTime);

	/** Build and queue a DataAck1 for the given sequence number. */
	void SendDataAck(uint16 Sequence);

	/** Unbundle a DataChannel 0x0019 multi-message block into IncomingMessages. */
	void UnbundleMessages(const uint8* Data, int32 Len);

	/** Timestamp (ms) of last retransmit check; used to avoid tight-looping in Tick. */
	double LastRetransmitCheckTime = 0.0;
};
