#include "Network/Handler/SWGReliabilityComponent.h"
#include "Network/SWGSessionOp.h"
#include "Network/SWGSession.h"
#include "Network/SWGPacket.h"
#include "HAL/PlatformTime.h"

// Retransmit unacked packets after this many milliseconds (SOE protocol standard: 700 ms).
static constexpr uint64 kResendIntervalMs = 700;

static FORCEINLINE uint64 NowMs()
{
	return (uint64)(FPlatformTime::Seconds() * 1000.0);
}

FSWGReliabilityComponent::FSWGReliabilityComponent(FSWGSession* InSession)
	: HandlerComponent(FName(TEXT("SWGReliability")))
	, Session(InSession)
{
}

void FSWGReliabilityComponent::Initialize()
{
	Initialized();
}

bool FSWGReliabilityComponent::IsValid() const
{
	return Session != nullptr;
}

// ── Incoming ─────────────────────────────────────────────────────────────────

void FSWGReliabilityComponent::Incoming(FBitReader& Packet)
{
	const int32 NumBytes = (int32)Packet.GetNumBytes();
	if (NumBytes < 2)
		return;

	const uint8* Data = Packet.GetData();

	// Only session packets reach this stage (fastpath packets have Data[0] != 0x00
	// and are game messages that need no reliability processing here).
	if (!SWGIsSessionPacket(Data, NumBytes))
		return;

	const ESWGSessionOp Op = SWGGetSessionOp(Data);

	if (Op == ESWGSessionOp::MultiPacket)
	{
		HandleMultiPacket(Packet);
	}
	else if (SWGOpIsDataChannel(Op))
	{
		HandleDataChannel(Packet);
	}
	else if (SWGOpIsDataFrag(Op))
	{
		HandleDataFrag(Packet);
	}
	else if (SWGOpIsDataAck(Op))
	{
		HandleDataAck(Packet);
	}
	// Other ops (Disconnect, Ping, NetStat, etc.) pass through to the Handshake stage.
}

void FSWGReliabilityComponent::HandleDataAck(FBitReader& Packet)
{
	const uint8* Data = Packet.GetData();
	if ((int32)Packet.GetNumBytes() < 4)
		return;

	// Acked sequence — big-endian uint16 at offset 2.
	const FSWGSeqNum AckedSN(((uint16)Data[2] << 8) | (uint16)Data[3]);

	// Remove all window packets whose sequence number <= AckedSN (cumulative ACK).
	// FSWGSeqNum::operator>= uses modular arithmetic so wrap-around at 0xFFFF is handled correctly.
	{
		FScopeLock Lock(&Session->WindowLock);
		Session->WindowPackets.RemoveAll([AckedSN](const FSWGPacket& WP) -> bool
		{
			if (WP.Data.Num() < 4) return true; // malformed, remove
			const FSWGSeqNum WPSeq(((uint16)WP.Data[2] << 8) | (uint16)WP.Data[3]);
			return AckedSN >= WPSeq;
		});
	}

	UE_LOG(LogTemp, Verbose, TEXT("FSWGReliabilityComponent: DataAck seq=%u"), AckedSN.Get());
}

void FSWGReliabilityComponent::HandleDataChannel(FBitReader& Packet)
{
	const uint8* Data = Packet.GetData();
	const int32 NumBytes = (int32)Packet.GetNumBytes();
	if (NumBytes < 4)
		return;

	const FSWGSeqNum IncomingSeq(((uint16)Data[2] << 8) | (uint16)Data[3]);

	if (IncomingSeq != Session->InSeqNext)
	{
		// Out-of-order or duplicate — drop for Phase 1 (no reorder buffer).
		UE_LOG(LogTemp, Verbose,
			TEXT("FSWGReliabilityComponent: DataChannel seq=%u expected=%u — dropped"),
			IncomingSeq.Get(), Session->InSeqNext.Get());
		return;
	}

	++Session->InSeqNext;
	SendDataAck(IncomingSeq.Get());

	const int32 PayloadOffset = 4; // after op(2) + seq(2)
	const int32 PayloadLen = NumBytes - PayloadOffset;
	if (PayloadLen <= 0)
		return;

	// Check for a multi-message bundle: SWGMultiMessageBundleMarker ([0x00][0x19]) at payload start.
	const uint16 BundleCheck = PayloadLen >= 2
		? (((uint16)Data[PayloadOffset] << 8) | (uint16)Data[PayloadOffset + 1])
		: 0u;

	if (BundleCheck == SWGMultiMessageBundleMarker)
	{
		// Skip the 2-byte bundle marker; sub-messages follow.
		UnbundleMessages(Data + PayloadOffset + 2, PayloadLen - 2);
	}
	else if (PayloadLen > 0)
	{
		FSWGPacket Msg;
		Msg.Data.Append(Data + PayloadOffset, PayloadLen);
		Session->IncomingMessages.Enqueue(MoveTemp(Msg));
	}
}

void FSWGReliabilityComponent::HandleDataFrag(FBitReader& Packet)
{
	const uint8* Data = Packet.GetData();
	const int32 NumBytes = (int32)Packet.GetNumBytes();
	if (NumBytes < 4)
		return;

	const FSWGSeqNum IncomingSeq(((uint16)Data[2] << 8) | (uint16)Data[3]);

	if (IncomingSeq != Session->InSeqNext)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("FSWGReliabilityComponent: DataFrag seq=%u expected=%u — dropped"),
			IncomingSeq.Get(), Session->InSeqNext.Get());
		return;
	}

	++Session->InSeqNext;
	SendDataAck(IncomingSeq.Get());

	if (Session->FragTotalSize == 0)
	{
		// First fragment: offset 4 holds BE uint32 total size, data starts at offset 8.
		if (NumBytes < 8)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGReliabilityComponent: first DataFrag too short"));
			return;
		}
		Session->FragTotalSize =
			((uint32)Data[4] << 24) | ((uint32)Data[5] << 16) |
			((uint32)Data[6] << 8)  | ((uint32)Data[7]);

		const int32 ChunkLen = NumBytes - 8;
		Session->FragBuffer.Reset();
		Session->FragBuffer.Append(Data + 8, ChunkLen);
		Session->FragCurrentSize = (uint32)ChunkLen;
	}
	else
	{
		// Continuation fragment: data starts at offset 4 (after op + seq).
		const int32 ChunkLen = NumBytes - 4;
		Session->FragBuffer.Append(Data + 4, ChunkLen);
		Session->FragCurrentSize += (uint32)ChunkLen;
	}

	if (Session->FragCurrentSize >= Session->FragTotalSize)
	{
		// Reassembly complete — push to IncomingMessages.
		FSWGPacket Msg;
		Msg.Data = MoveTemp(Session->FragBuffer);
		Session->IncomingMessages.Enqueue(MoveTemp(Msg));

		Session->FragTotalSize = 0;
		Session->FragCurrentSize = 0;
		Session->FragBuffer.Empty();
	}
}

void FSWGReliabilityComponent::HandleMultiPacket(FBitReader& Packet)
{
	const uint8* Data = Packet.GetData();
	const int32 NumBytes = (int32)Packet.GetNumBytes();

	// Sub-packets start at offset 2 (after MultiPacket op [0x00][0x03]).
	int32 Offset = 2;
	while (Offset < NumBytes)
	{
		const int32 SubSize = (int32)Data[Offset++];
		if (SubSize == 0 || Offset + SubSize > NumBytes)
			break;

		// Each sub-packet is a complete packet with its own op — process recursively.
		FBitReader SubReader(const_cast<uint8*>(Data + Offset), (int64)SubSize * 8);
		Incoming(SubReader);
		Offset += SubSize;
	}
}

// ── Outgoing ─────────────────────────────────────────────────────────────────

void FSWGReliabilityComponent::Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits)
{
	const int32 NumBytes = (int32)Packet.GetNumBytes();
	if (NumBytes < 4)
		return;

	const uint8* Data = Packet.GetData();
	if (!SWGIsSessionPacket(Data, NumBytes))
		return;

	const ESWGSessionOp Op = SWGGetSessionOp(Data);
	if (!SWGOpIsReliable(Op))
		return;

	// Buffer a copy of the pre-compression/pre-encryption packet for potential resend.
	FSWGPacket WinPacket;
	WinPacket.Data.Append(Data, NumBytes);
	WinPacket.TimeSent = NowMs();
	WinPacket.bEncrypted = true;

	FScopeLock Lock(&Session->WindowLock);
	Session->WindowPackets.Add(MoveTemp(WinPacket));
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void FSWGReliabilityComponent::Tick(float DeltaTime)
{
	HandleRetransmits(DeltaTime);
}

void FSWGReliabilityComponent::HandleRetransmits(float DeltaTime)
{
	const uint64 Now = NowMs();

	// Collect packets that need resending (under lock), then enqueue them (outside lock).
	TArray<FSWGPacket> ToResend;
	{
		FScopeLock Lock(&Session->WindowLock);
		for (FSWGPacket& WP : Session->WindowPackets)
		{
			if (WP.TimeSent == 0)
				continue;

			if (Now - WP.TimeSent > kResendIntervalMs)
			{
				FSWGPacket Copy;
				Copy.Data = WP.Data;
				Copy.bEncrypted = true;
				ToResend.Add(MoveTemp(Copy));

				WP.TimeSent = Now;
				WP.Resends++;
			}
		}
	}

	for (FSWGPacket& P : ToResend)
	{
		UE_LOG(LogTemp, Verbose, TEXT("FSWGReliabilityComponent: retransmitting window packet (resend #%u)"), P.Resends);
		Session->OutgoingReliable.Enqueue(MoveTemp(P));
	}
}

int32 FSWGReliabilityComponent::GetReservedPacketBits() const
{
	return 0; // Sequence numbers are in the packet the game layer already built.
}

// ── Private helpers ───────────────────────────────────────────────────────────

void FSWGReliabilityComponent::SendDataAck(uint16 Sequence)
{
	FSWGPacket Ack;
	Ack.WriteByte(0x00);
	Ack.WriteByte(static_cast<uint8>(ESWGSessionOp::DataAck1));
	Ack.WriteByte((uint8)(Sequence >> 8));
	Ack.WriteByte((uint8)(Sequence & 0xFF));
	Ack.bEncrypted = true;
	Session->OutgoingUnreliable.Enqueue(MoveTemp(Ack));
}

void FSWGReliabilityComponent::UnbundleMessages(const uint8* Data, int32 Len)
{
	// DataChannel 0x0019 sub-message bundle format:
	//   [size_byte]  — if == 0xFF: next 2 bytes are BE uint16 actual size
	//   [priority]   — 1 byte (included in size)
	//   [pad]        — 1 byte (included in size)
	//   [message...]  — (size - 2) bytes
	int32 Offset = 0;
	while (Offset < Len)
	{
		int32 SlotSize = (int32)Data[Offset++];
		if (SlotSize == 0)
			break;

		if (SlotSize == 0xFF && Offset + 2 <= Len)
		{
			SlotSize = ((int32)Data[Offset] << 8) | (int32)Data[Offset + 1];
			Offset += 2;
		}

		if (SlotSize < 2 || Offset + SlotSize > Len)
			break;

		// Skip 2-byte header (priority + pad); remaining bytes are the game message.
		const int32 MsgOffset = Offset + 2;
		const int32 MsgSize   = SlotSize - 2;

		if (MsgSize > 0)
		{
			FSWGPacket Msg;
			Msg.Data.Append(Data + MsgOffset, MsgSize);
			Session->IncomingMessages.Enqueue(MoveTemp(Msg));
		}

		Offset += SlotSize;
	}
}
