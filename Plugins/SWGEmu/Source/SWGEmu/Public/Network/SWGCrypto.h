#pragma once

#include "CoreMinimal.h"

/**
 * FSWGCrypto provides encryption, decryption, and CRC functions for the SOE protocol.
 *
 * SOE uses:
 * - A chained XOR stream cipher (NOT standard RC4) seeded by the EncryptionKey
 * - CRC32 for packet integrity checking
 * - zlib compression for packet payload data
 *
 * All functions operate in-place on the buffer.
 */
struct FSWGCrypto
{
	/**
	 * Decrypt a buffer in-place using the SOE XOR cipher.
	 *
	 * The cipher processes 4-byte blocks: XOR with Seed, then use the ciphertext
	 * block as the next seed. Remaining 1-3 bytes XOR with the raw seed value.
	 *
	 * @param Data        Buffer to decrypt (modified in-place)
	 * @param Length      Number of bytes to decrypt
	 * @param Seed        Initial key (typically EncryptionKey from SessionResponse)
	 * @param StartIndex  Offset in Data where decryption begins (usually 2, to skip session op)
	 */
	static void Decrypt(uint8* Data, uint32 Length, uint32 Seed, uint32 StartIndex = 0);

	/**
	 * Encrypt a buffer in-place using the SOE XOR cipher.
	 * Inverse of Decrypt.
	 */
	static void Encrypt(uint8* Data, uint32 Length, uint32 Seed, uint32 StartIndex = 0);

	/**
	 * Generate a CRC32 checksum over a buffer.
	 *
	 * Used to verify packet integrity. CRC is appended (unencrypted) to the packet.
	 *
	 * @param Data       Buffer to checksum
	 * @param Length     Number of bytes to include
	 * @param Seed       Initial seed value (usually 0 for packets)
	 * @param StartIndex Offset to begin CRC computation
	 * @return CRC32 value (takes last 2 bytes for transmission)
	 */
	static uint32 GenerateCRC(const uint8* Data, uint32 Length, uint32 Seed = 0, uint32 StartIndex = 0);

	/**
	 * Decompress data using zlib (inflate).
	 *
	 * @param In      Compressed input buffer (zlib format with 0x78 header)
	 * @param InLen   Size of compressed data
	 * @param Out     Decompressed output buffer (must be pre-allocated)
	 * @param OutLen  Capacity of output buffer
	 * @return        Number of bytes decompressed, or 0 on error
	 */
	static int32 Decompress(const uint8* In, int32 InLen, uint8* Out, int32 OutLen);

	/**
	 * Compress data using zlib (deflate, level 6).
	 *
	 * @param In      Uncompressed input buffer
	 * @param InLen   Size of input data
	 * @param Out     Compressed output buffer (must be pre-allocated, larger than InLen)
	 * @param OutLen  Capacity of output buffer
	 * @return        Number of bytes compressed, or 0 if compression would increase size
	 */
	static int32 Compress(const uint8* In, int32 InLen, uint8* Out, int32 OutLen);

private:
	// CRC32 lookup table (256 entries, computed from polynomial)
	static const uint32 CRCTable[256];
};
