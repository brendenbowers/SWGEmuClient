#pragma once

#include "CoreMinimal.h"

/**
 * FSWGPacket represents a raw SOE protocol packet buffer with read/write cursors.
 *
 * The SOE protocol uses big-endian (network byte order) on the wire. All integer
 * reads/writes automatically swap bytes via NETWORK_ORDER macros.
 *
 */
struct FSWGPacket
{
	// Raw packet data
	TArray<uint8> Data;

	// Cursor positions
	int32 ReadIndex = 0;
	int32 WriteIndex = 0;

	// Packet flags
	bool bEncrypted = false;
	bool bCompressed = false;
	uint32 CRC = 0;

	// Timing for reliability tracking
	uint64 TimeCreated = 0;
	uint64 TimeSent = 0;
	uint32 Resends = 0;

	// Construction
	FSWGPacket();
	explicit FSWGPacket(int32 Size);
	FSWGPacket(const uint8* Bytes, int32 Len);

	// Capacity and sizing
	void SetSize(int32 NewSize);
	void ShrinkToWriteIndex();
	void Reset();

	// ── Read Helpers ──────────────────────────────────────────
	uint8 ReadByte();
	int16 ReadInt16();
	uint16 ReadUInt16();
	int32 ReadInt32();
	uint32 ReadUInt32();
	int64 ReadInt64();
	uint64 ReadUInt64();
	float ReadFloat();
	FString ReadAsciiString();
	FString ReadUnicodeString();
	void Skip(int32 NumBytes);

	// ── Write Helpers ─────────────────────────────────────────
	void WriteByte(uint8 Value);
	void WriteInt16(int16 Value);
	void WriteUInt16(uint16 Value);
	void WriteInt32(int32 Value);
	void WriteUInt32(uint32 Value);
	void WriteInt64(int64 Value);
	void WriteUInt64(uint64 Value);
	void WriteFloat(float Value);
	void WriteAsciiString(const FString& Value);
	void WriteUnicodeString(const FString& Value);

	// Utility
	int32 GetSize() const { return Data.Num(); }
	int32 GetRemaining() const { return Data.Num() - ReadIndex; }
	bool IsAtEnd() const { return ReadIndex >= Data.Num(); }

	FString ToString() const;

private:
	void EnsureCapacity(int32 NumBytesNeeded);
};
