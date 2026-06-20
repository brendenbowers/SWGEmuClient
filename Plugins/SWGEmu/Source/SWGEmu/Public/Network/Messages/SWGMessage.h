#pragma once

#include "CoreMinimal.h"
#include "../SWGPacket.h"

/**
 * FSWGMessage wraps a game-layer message packet (after SOE protocol layer has
 * decrypted, decompressed, and reassembled it).
 *
 * Game messages carry a MessageOp opcode (uint32) that identifies the message type.
 * The opcode lives at byte offset 2-5 (after the 2-byte session sub-type prefix).
 */
struct FSWGMessage
{
	TArray<uint8> Data;
	int32 ReadIndex = 0;
	uint32 Opcode = 0;

	FSWGMessage() = default;

	/**
	 * Construct from a fully-assembled packet.
	 * Reads and caches the opcode.
	 */
	explicit FSWGMessage(const FSWGPacket& Packet);

	/**
	 * Peek at the opcode without constructing a message.
	 * Useful for routing before full parse.
	 */
	static uint32 PeekOpcode(const FSWGPacket& Packet);

	/**
	 * Read helpers (re-exposing packet operations with current ReadIndex).
	 */
	uint8 ReadByte() { return Data.IsValidIndex(ReadIndex) ? Data[ReadIndex++] : 0; }
	int16 ReadInt16();
	uint16 ReadUInt16();
	int32 ReadInt32();
	uint32 ReadUInt32();
	int64 ReadInt64();
	uint64 ReadUInt64();
	float ReadFloat();
	FString ReadAsciiString();
	FString ReadUnicodeString();
	void Skip(int32 NumBytes) { ReadIndex += NumBytes; }

	// Utility
	int32 GetRemaining() const { return Data.Num() - ReadIndex; }
	bool IsAtEnd() const { return ReadIndex >= Data.Num(); }

	FString ToString() const;
};
