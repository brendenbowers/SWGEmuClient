#pragma once

#include "CoreMinimal.h"
#include "Serialization/Archive.h"

/**
 * FSWGPacket — SOE protocol packet buffer.
 *
 * Derives from FArchive so that Phase 2 game messages can serialize with
 * operator<< (ArForceByteSwapping = false — SWG game layer is little-endian;
 * Core3 uses memcpy/host-byte-order for all game fields; only SOE headers
 * use htons/htonl which are handled by the SOE protocol layer separately).
 *
 * FArchive is a single-cursor streaming type: you are either saving (writing)
 * or loading (reading). The single private cursor `Pos` replaces the old
 * ReadIndex / WriteIndex pair:
 *
 *   Writing:   FSWGPacket Pkt;               // starts in saving mode
 *              Pkt.WriteByte(0x00);           // or Pkt << MyUInt32;
 *              // Data[0..Pos-1] holds the written bytes
 *
 *   Reading:   Pkt.SetReadMode();            // Seek(0), flip to loading
 *              uint32 V; Pkt << V;            // big-endian read
 *
 * The `Data` member is public so that handlers and the reliability component
 * can append raw bytes (e.g. fragment reassembly) without going through the
 * cursor. Direct array manipulation does not advance Pos — callers set the
 * mode and position explicitly with SetReadMode() / SetWriteMode() / Seek().
 */
struct SWGEMU_API FSWGPacket : public FArchive
{
	// Raw packet bytes. Public so handlers can Append / MoveTemp / index directly.
	TArray<uint8> Data;

	// Packet metadata (not wire data)
	bool   bEncrypted  = false;
	bool   bCompressed = false;
	uint32 CRC         = 0;
	uint64 TimeCreated = 0;
	uint64 TimeSent    = 0;
	uint32 Resends     = 0;

	// ── Construction ──────────────────────────────────────────────
	FSWGPacket();
	FSWGPacket(const uint8* Bytes, int32 Len); // Loads existing bytes, starts in read mode

	// ── Mode ─────────────────────────────────────────────────────
	/** Switch to read mode and seek back to byte 0. */
	void SetReadMode()  { SetIsLoading(true);  SetIsSaving(false); Pos = 0; }
	/** Switch to write mode (position stays where it is). */
	void SetWriteMode() { SetIsLoading(false); SetIsSaving(true); }

	// ── FArchive interface ────────────────────────────────────────
	virtual FString GetArchiveName() const override { return TEXT("FSWGPacket"); }
	virtual void    Serialize(void* V, int64 Length) override;
	virtual int64   Tell()    override { return Pos; }
	virtual void    Seek(int64 InPos) override { Pos = InPos; }
	virtual int64   TotalSize() override { return (int64)Data.Num(); }

	// ── Sizing ────────────────────────────────────────────────────
	/** Trim Data to however many bytes have been written (Pos). */
	void ShrinkToEnd();
	void Reset();

	// ── Typed read helpers (big-endian) ──────────────────────────
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

	// ── Typed write helpers (big-endian) ─────────────────────────
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

	// ── Utility ───────────────────────────────────────────────────
	int32 GetSize()      const { return Data.Num(); }
	int32 GetRemaining() const { return Data.Num() - (int32)Pos; }
	bool  IsAtEnd()      const { return Pos >= (int64)Data.Num(); }

	FString ToString() const;

private:
	int64 Pos = 0; // Single FArchive-style cursor
};
