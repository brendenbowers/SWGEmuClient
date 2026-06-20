#pragma once

#include "CoreMinimal.h"

/**
 * ESWGSessionOp enumerates SOE UDP protocol session opcodes.
 *
 * Session packets have the form [0x00][op][payload...]. This enum represents
 * the second byte (Data[1]) — the actual op discriminator.
 * (e.g. SessionRequest = [0x00][0x01], so the op byte is 0x01).
 */
enum class ESWGSessionOp : uint8
{
	SessionRequest      = 0x01,
	SessionResponse     = 0x02,
	MultiPacket         = 0x03,
	Disconnect          = 0x05,
	Ping                = 0x06,
	NetStatRequest      = 0x07,
	NetStatResponse     = 0x08,
	DataChannel1        = 0x09,
	DataChannel2        = 0x0A,
	DataChannel3        = 0x0B,
	DataChannel4        = 0x0C,
	DataFrag1           = 0x0D,
	DataFrag2           = 0x0E,
	DataFrag3           = 0x0F,
	DataFrag4           = 0x10,
	DataOrder1          = 0x11,
	DataOrder2          = 0x12,
	DataOrder3          = 0x13,
	DataOrder4          = 0x14,
	DataAck1            = 0x15,
	DataAck2            = 0x16,
	DataAck3            = 0x17,
	DataAck4            = 0x18,
	FatalError          = 0x19,
	FatalErrorResponse  = 0x1A,
	Reset               = 0x1D,
	CriticalError       = 0x1E,
};

/**
 * Marker written as a BE uint16 ([0x00][0x19]) at the start of a DataChannel
 * payload to indicate a multi-message bundle.
 * Distinct from the FatalError packet op — this is payload data, not a packet header.
 */
inline constexpr uint16 SWGMultiMessageBundleMarker = 0x0019;

/** Compression flag byte appended before the CRC trailer. 0x01 = compressed, 0x00 = not. */
inline constexpr uint8 SWGCompressionFlagEnabled  = 0x01;
inline constexpr uint8 SWGCompressionFlagDisabled = 0x00;

// ── Packet-level helpers ──────────────────────────────────────────────────────

/** Returns true if this buffer starts with the SOE session packet prefix (Data[0] == 0x00). */
FORCEINLINE bool SWGIsSessionPacket(const uint8* Data, int32 NumBytes)
{
	return NumBytes >= 2 && Data[0] == 0x00;
}

/** Extract the session op byte from a packet that has already been validated as a session packet. */
FORCEINLINE ESWGSessionOp SWGGetSessionOp(const uint8* Data)
{
	return static_cast<ESWGSessionOp>(Data[1]);
}

/**
 * Returns the byte offset at which encryption/compression begins.
 * Session packets skip the 2-byte op prefix; fastpath packets skip 1 byte.
 */
FORCEINLINE uint32 SWGGetPayloadStartOffset(const uint8* Data)
{
	return (Data[0] == 0x00) ? 2u : 1u;
}

// ── Op-class predicates ───────────────────────────────────────────────────────

FORCEINLINE bool SWGOpIsDataChannel(ESWGSessionOp Op)
{
	return Op >= ESWGSessionOp::DataChannel1 && Op <= ESWGSessionOp::DataChannel4;
}

FORCEINLINE bool SWGOpIsDataFrag(ESWGSessionOp Op)
{
	return Op >= ESWGSessionOp::DataFrag1 && Op <= ESWGSessionOp::DataFrag4;
}

FORCEINLINE bool SWGOpIsDataAck(ESWGSessionOp Op)
{
	return Op >= ESWGSessionOp::DataAck1 && Op <= ESWGSessionOp::DataAck4;
}

FORCEINLINE bool SWGOpIsDataOrder(ESWGSessionOp Op)
{
	return Op >= ESWGSessionOp::DataOrder1 && Op <= ESWGSessionOp::DataOrder4;
}

/** True for ops that are sent reliably and must be buffered for potential retransmit. */
FORCEINLINE bool SWGOpIsReliable(ESWGSessionOp Op)
{
	return SWGOpIsDataChannel(Op) || SWGOpIsDataFrag(Op);
}
