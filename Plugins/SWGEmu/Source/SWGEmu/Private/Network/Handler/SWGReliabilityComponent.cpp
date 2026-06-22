#include "Network/Handler/SWGReliabilityComponent.h"
#include "Network/SWGSessionOp.h"
#include "Network/SWGSession.h"
#include "Network/SWGPacket.h"
#include "HAL/PlatformTime.h"

// Retransmit unacked packets after 700 ms (SOE protocol standard).
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
	OutSeqNext    = FSWGSeqNum(0);
	InSeqNext     = FSWGSeqNum(0);
	LastSeqAcked  = 0;
	FragTotalSize = 0;
	FragCurrentSize = 0;
	FragBuffer.Empty();
	WindowPackets.Empty();
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

	if (!SWGIsSessionPacket(Data, NumBytes))
		return;

	const ESWGSessionOp Op = SWGGetSessionOp(Data);

	if (Op == ESWGSessionOp::MultiPacket)
		HandleMultiPacket(Packet);
	else if (SWGOpIsDataChannel(Op))
		HandleDataChannel(Packet);
	else if (SWGOpIsDataFrag(Op))
		HandleDataFrag(Packet);
	else if (SWGOpIsDataAck(Op))
		HandleDataAck(Packet);
	// Other ops (SessionResponse, Disconnect, Ping…) pass through to the Handshake stage.
}

void FSWGReliabilityComponent::HandleDataAck(FBitReader& Packet)
{
	const uint8* Data = Packet.GetData();
	if ((int32)Packet.GetNumBytes() < 4)
		return;

	const FSWGSeqNum AckedSN(((uint16)Data[2] << 8) | (uint16)Data[3]);

	FScopeLock Lock(&WindowLock);
	WindowPackets.RemoveAll([AckedSN](const FSWGPacket& WP) -> bool
	{
		if (WP.Data.Num() < 4) return true;
		const FSWGSeqNum WPSeq(((uint16)WP.Data[2] << 8) | (uint16)WP.Data[3]);
		return AckedSN >= WPSeq;
	});

	UE_LOG(LogTemp, Verbose, TEXT("FSWGReliabilityComponent: DataAck seq=%u"), AckedSN.Get());
}

void FSWGReliabilityComponent::HandleDataChannel(FBitReader& Packet)
{
	const uint8* Data = Packet.GetData();
	const int32 NumBytes = (int32)Packet.GetNumBytes();
	if (NumBytes < 4)
		return;

	const FSWGSeqNum IncomingSeq(((uint16)Data[2] << 8) | (uint16)Data[3]);

	if (IncomingSeq != InSeqNext)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("FSWGReliabilityComponent: DataChannel seq=%u expected=%u — out of order"),
			IncomingSeq.Get(), InSeqNext.Get());
		// Send DataOrder to inform server of what we're expecting
		SendDataOrder(InSeqNext.Get());
		return;
	}

	++InSeqNext;
	SendDataAck(IncomingSeq.Get());

	const int32 PayloadOffset = 4; // op(2) + seq(2)
	const int32 PayloadLen    = NumBytes - PayloadOffset;
	if (PayloadLen <= 0)
		return;

	const uint16 BundleCheck = (PayloadLen >= 2)
		? (((uint16)Data[PayloadOffset] << 8) | (uint16)Data[PayloadOffset + 1])
		: 0u;

	if (BundleCheck == SWGMultiMessageBundleMarker)
	{
		UnbundleMessages(Data + PayloadOffset + 2, PayloadLen - 2);
	}
	else
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

	if (IncomingSeq != InSeqNext)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("FSWGReliabilityComponent: DataFrag seq=%u expected=%u — out of order"),
			IncomingSeq.Get(), InSeqNext.Get());
		// Send DataOrder to inform server of what we're expecting
		SendDataOrder(InSeqNext.Get());
		return;
	}

	++InSeqNext;
	SendDataAck(IncomingSeq.Get());

	if (FragTotalSize == 0)
	{
		// First fragment: bytes 4-7 are BE uint32 total reassembled size.
		if (NumBytes < 8)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGReliabilityComponent: first DataFrag too short"));
			return;
		}
		FragTotalSize =
			((uint32)Data[4] << 24) | ((uint32)Data[5] << 16) |
			((uint32)Data[6] << 8)  | ((uint32)Data[7]);

		const int32 ChunkLen = NumBytes - 8;
		FragBuffer.Reset();
		FragBuffer.Append(Data + 8, ChunkLen);
		FragCurrentSize = (uint32)ChunkLen;
	}
	else
	{
		// Continuation: data starts after op(2)+seq(2).
		const int32 ChunkLen = NumBytes - 4;
		FragBuffer.Append(Data + 4, ChunkLen);
		FragCurrentSize += (uint32)ChunkLen;
	}

	if (FragCurrentSize >= FragTotalSize)
	{
		FSWGPacket Msg;
		Msg.Data = MoveTemp(FragBuffer);
		Session->IncomingMessages.Enqueue(MoveTemp(Msg));

		FragTotalSize   = 0;
		FragCurrentSize = 0;
		FragBuffer.Empty();
	}
}

void FSWGReliabilityComponent::HandleMultiPacket(FBitReader& Packet)
{
	const uint8* Data = Packet.GetData();
	const int32 NumBytes = (int32)Packet.GetNumBytes();

	int32 Offset = 2; // skip MultiPacket op [0x00][0x03]
	while (Offset < NumBytes)
	{
		const int32 SubSize = (int32)Data[Offset++];
		if (SubSize == 0 || Offset + SubSize > NumBytes)
			break;

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

	// Buffer a pre-pipeline copy for potential retransmit.
	FSWGPacket WinPacket;
	WinPacket.Data.Append(Data, NumBytes);
	WinPacket.TimeSent = NowMs();

	FScopeLock Lock(&WindowLock);
	WindowPackets.Add(MoveTemp(WinPacket));
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void FSWGReliabilityComponent::Tick(float DeltaTime)
{
	HandleRetransmits(DeltaTime);
}

void FSWGReliabilityComponent::HandleRetransmits(float DeltaTime)
{
	const uint64 Now = NowMs();

	TArray<FSWGPacket> ToResend;
	{
		FScopeLock Lock(&WindowLock);
		for (FSWGPacket& WP : WindowPackets)
		{
			if (WP.TimeSent == 0)
				continue;
			if (Now - WP.TimeSent > kResendIntervalMs)
			{
				FSWGPacket Copy;
				Copy.Data = WP.Data;
				ToResend.Add(MoveTemp(Copy));

				WP.TimeSent = Now;
				WP.Resends++;
			}
		}
	}

	for (FSWGPacket& P : ToResend)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("FSWGReliabilityComponent: retransmitting window packet (resend #%u)"), P.Resends);
		Session->OutgoingReliable.Enqueue(MoveTemp(P));
	}
}

int32 FSWGReliabilityComponent::GetReservedPacketBits() const
{
	return 0; // Sequence numbers are part of the packet the game layer builds.
}

// ── Private helpers ───────────────────────────────────────────────────────────

void FSWGReliabilityComponent::SendDataAck(uint16 Sequence)
{
	FSWGPacket Ack;
	Ack.WriteByte(0x00);
	Ack.WriteByte(static_cast<uint8>(ESWGSessionOp::DataAck1));
	Ack.WriteByte((uint8)(Sequence >> 8));
	Ack.WriteByte((uint8)(Sequence & 0xFF));
	Session->OutgoingUnreliable.Enqueue(MoveTemp(Ack));
}

void FSWGReliabilityComponent::SendDataOrder(uint16 Sequence)
{
	FSWGPacket Order;
	Order.WriteByte(0x00);
	Order.WriteByte(static_cast<uint8>(ESWGSessionOp::DataOrder1));
	Order.WriteByte((uint8)(Sequence >> 8));
	Order.WriteByte((uint8)(Sequence & 0xFF));
	Session->OutgoingUnreliable.Enqueue(MoveTemp(Order));
}

void FSWGReliabilityComponent::UnbundleMessages(const uint8* Data, int32 Len)
{
	// Sub-message format: [size] [priority(1)] [pad(1)] [message(size-2)]
	// If size == 0xFF: next 2 bytes are BE uint16 actual size.
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

		const int32 MsgOffset = Offset + 2; // skip priority + pad
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
