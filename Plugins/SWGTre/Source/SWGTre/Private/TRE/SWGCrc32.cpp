#include "TRE/SWGCrc32.h"

uint32 FSWGCrc32::Table[256] = {};
bool FSWGCrc32::bTableInitialized = false;

void FSWGCrc32::EnsureTable()
{
	if (bTableInitialized)
	{
		return;
	}

	constexpr uint32 Poly = 0x04C11DB7;
	for (uint32 i = 0; i < 256; ++i)
	{
		uint32 Crc = i << 24;
		for (int32 Bit = 0; Bit < 8; ++Bit)
		{
			Crc = (Crc & 0x80000000) ? ((Crc << 1) ^ Poly) : (Crc << 1);
		}
		Table[i] = Crc;
	}
	bTableInitialized = true;
}

uint32 FSWGCrc32::HashString(const ANSICHAR* Value)
{
	EnsureTable();

	uint32 Crc = 0xFFFFFFFF;
	for (const ANSICHAR* P = Value; *P; ++P)
	{
		Crc = Table[((Crc >> 24) ^ (uint8)(*P)) & 0xFF] ^ (Crc << 8);
	}
	return ~Crc;
}

uint32 FSWGCrc32::HashString(const FString& Value)
{
	return HashString(TCHAR_TO_ANSI(*Value));
}
