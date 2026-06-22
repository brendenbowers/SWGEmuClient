#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

/**
 * FSWGMessage wraps a game-layer message packet (after the SOE protocol layer has
 * decrypted, decompressed, and reassembled it).
 *
 * Owns an FSWGPacket for storage and cursor management. All typed read helpers
 * delegate to the packet so byte-order conversion is handled by FArchive's
 * ArForceByteSwapping — no NETWORK_ORDER macros needed.
 *
 * Layout assumed on construction:
 *   [0x00][sub-op(1)][opcode(4)][payload...]
 * The cursor is positioned at byte 6 (payload start) after construction.
 */
struct FSWGMessage
{
	uint32 Opcode = 0;

	FSWGMessage() = default;
	explicit FSWGMessage(const FSWGPacket& InPacket);

	/** Peek at the opcode without constructing a full message. */
	static uint32 PeekOpcode(const FSWGPacket& Packet);

	// ── Typed read helpers — delegate to FSWGPacket ──────────────
	uint8   ReadByte();
	int16   ReadInt16();
	uint16  ReadUInt16();
	int32   ReadInt32();
	uint32  ReadUInt32();
	int64   ReadInt64();
	uint64  ReadUInt64();
	float   ReadFloat();
	FString ReadAsciiString();
	FString ReadUnicodeString();
	void    Skip(int32 NumBytes);

	// ── Archive-style operators — proxy to packet ──────────────────
	FSWGMessage& operator<<(uint8& Value)    { Packet << Value; return *this; }
	FSWGMessage& operator<<(int16& Value)    { Packet << Value; return *this; }
	FSWGMessage& operator<<(uint16& Value)   { Packet << Value; return *this; }
	FSWGMessage& operator<<(int32& Value)    { Packet << Value; return *this; }
	FSWGMessage& operator<<(uint32& Value)   { Packet << Value; return *this; }
	FSWGMessage& operator<<(int64& Value)    { Packet << Value; return *this; }
	FSWGMessage& operator<<(uint64& Value)   { Packet << Value; return *this; }
	FSWGMessage& operator<<(float& Value)    { Packet << Value; return *this; }
	FSWGMessage& operator<<(FString& Value)  { Packet << Value; return *this; }
	FSWGMessage& operator<<(bool& Value)     { Packet << Value; return *this; }

	// ── Alternate >> syntax (convention varies; both work) ──────────
	FSWGMessage& operator>>(uint8& Value)    { Packet << Value; return *this; }
	FSWGMessage& operator>>(int16& Value)    { Packet << Value; return *this; }
	FSWGMessage& operator>>(uint16& Value)   { Packet << Value; return *this; }
	FSWGMessage& operator>>(int32& Value)    { Packet << Value; return *this; }
	FSWGMessage& operator>>(uint32& Value)   { Packet << Value; return *this; }
	FSWGMessage& operator>>(int64& Value)    { Packet << Value; return *this; }
	FSWGMessage& operator>>(uint64& Value)   { Packet << Value; return *this; }
	FSWGMessage& operator>>(float& Value)    { Packet << Value; return *this; }
	FSWGMessage& operator>>(FString& Value)  { Packet << Value; return *this; }
	FSWGMessage& operator>>(bool& Value)     { Packet << Value; return *this; }

	// ── Utility ───────────────────────────────────────────────────
	int32   GetRemaining() const;
	bool    IsAtEnd() const;
	FString ToString() const;

private:
	FSWGPacket Packet;
};
