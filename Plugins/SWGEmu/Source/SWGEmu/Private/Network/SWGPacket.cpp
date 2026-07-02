#include "Network/SWGPacket.h"

// ── Constructors ──────────────────────────────────────────────────────────────

FSWGPacket::FSWGPacket()
{
	SetIsSaving(true);
	// SWG game-layer protocol is little-endian (Core3 writePrimitive = memcpy in host
	// byte order; x86 Linux/Windows are both LE). SOE header fields use htons/htonl
	// separately — FSWGPacket only handles the game layer, so no byte-swapping here.
	ArForceByteSwapping = false;
}

FSWGPacket::FSWGPacket(const uint8* Bytes, int32 Len)
{
	Data.Append(Bytes, Len);
	SetIsLoading(true);
	ArForceByteSwapping = false;
	Pos = 0;
}

// ── FArchive::Serialize ───────────────────────────────────────────────────────

void FSWGPacket::Serialize(void* V, int64 Length)
{
	const int32 ByteLen = (int32)Length;
	if (IsLoading())
	{
		if (Pos + ByteLen > (int64)Data.Num())
		{
			SetError();
			return;
		}
		FMemory::Memcpy(V, Data.GetData() + Pos, ByteLen);
	}
	else
	{
		const int64 Needed = Pos + ByteLen;
		if (Needed > (int64)Data.Num())
		{
			// Reserve headroom as *capacity* (avoids per-write reallocation) but keep
			// Num exact — otherwise the extra bytes ship as trailing zero padding on
			// the wire, which corrupts CRC/DataAcks and breaks the reliable channel.
			Data.Reserve((int32)Needed + 64);
			Data.SetNum((int32)Needed, EAllowShrinking::No);
		}
		FMemory::Memcpy(Data.GetData() + Pos, V, ByteLen);
	}
	Pos += ByteLen;
}

// ── Sizing ────────────────────────────────────────────────────────────────────

void FSWGPacket::ShrinkToEnd()
{
	Data.SetNum((int32)Pos);
}

void FSWGPacket::Reset()
{
	Pos         = 0;
	bEncrypted  = false;
	bCompressed = false;
	CRC         = 0;
	TimeCreated = 0;
	TimeSent    = 0;
	Resends     = 0;
	SetIsSaving(true);
}

// ── Read helpers ──────────────────────────────────────────────────────────────
// operator<< on FArchive reads bytes into the variable then byte-swaps in place
// (because ArForceByteSwapping = true). We just need to ensure we're in loading mode.

uint8 FSWGPacket::ReadByte()
{
	uint8 V = 0;
	Serialize(&V, 1); // single byte — no swap needed
	return V;
}

int16 FSWGPacket::ReadInt16()
{
	int16 V = 0;
	*this << V;
	return V;
}

uint16 FSWGPacket::ReadUInt16()
{
	uint16 V = 0;
	*this << V;
	return V;
}

int32 FSWGPacket::ReadInt32()
{
	int32 V = 0;
	*this << V;
	return V;
}

uint32 FSWGPacket::ReadUInt32()
{
	uint32 V = 0;
	*this << V;
	return V;
}

int64 FSWGPacket::ReadInt64()
{
	int64 V = 0;
	*this << V;
	return V;
}

uint64 FSWGPacket::ReadUInt64()
{
	uint64 V = 0;
	*this << V;
	return V;
}

float FSWGPacket::ReadFloat()
{
	float V = 0.f;
	*this << V;
	return V;
}

FString FSWGPacket::ReadAsciiString()
{
	uint16 Len = 0;
	*this << Len; // big-endian length prefix
	if (IsError() || Pos + Len > (int64)Data.Num())
	{
		SetError();
		return FString();
	}

	TArray<ANSICHAR> Buf;
	Buf.SetNum(Len + 1);
	Serialize(Buf.GetData(), Len);
	Buf[Len] = '\0';
	return FString(ANSI_TO_TCHAR(Buf.GetData()));
}

FString FSWGPacket::ReadUnicodeString()
{
	// SWG unicode strings: int32 character count (4 bytes LE) + count*2 bytes UCS-2 LE
	int32 Len = 0;
	*this << Len;
	if (IsError() || Len < 0 || Pos + (int64)(Len * 2) > (int64)Data.Num())
	{
		SetError();
		return FString();
	}
	if (Len == 0)
		return FString();

	// Read raw UCS-2 LE bytes then convert to FString via UTF-16
	TArray<UCS2CHAR> Chars;
	Chars.SetNumUninitialized(Len + 1);
	Serialize(Chars.GetData(), Len * 2);
	Chars[Len] = 0;
	return FString(reinterpret_cast<const UCS2CHAR*>(Chars.GetData()));
}

void FSWGPacket::Skip(int32 NumBytes)
{
	Pos = FMath::Min(Pos + NumBytes, (int64)Data.Num());
}

// ── Write helpers ─────────────────────────────────────────────────────────────
// operator<< on FArchive byte-swaps the value then calls Serialize, giving
// big-endian output. We accept the value by copy so operator<< can modify it.

void FSWGPacket::WriteByte(uint8 Value)
{
	Serialize(&Value, 1);
}

void FSWGPacket::WriteInt16(int16 Value)
{
	*this << Value;
}

void FSWGPacket::WriteUInt16(uint16 Value)
{
	*this << Value;
}

void FSWGPacket::WriteInt32(int32 Value)
{
	*this << Value;
}

void FSWGPacket::WriteUInt32(uint32 Value)
{
	*this << Value;
}

void FSWGPacket::WriteInt64(int64 Value)
{
	*this << Value;
}

void FSWGPacket::WriteUInt64(uint64 Value)
{
	*this << Value;
}

void FSWGPacket::WriteFloat(float Value)
{
	*this << Value;
}

void FSWGPacket::WriteAsciiString(const FString& Value)
{
	auto Converted = StringCast<ANSICHAR>(*Value);
	const int32 Len = Converted.Length();
	uint16 Len16 = (uint16)Len;
	*this << Len16;
	Serialize(const_cast<ANSICHAR*>(Converted.Get()), Len);
}

void FSWGPacket::WriteUnicodeString(const FString& Value)
{
	// SWG unicode strings: int32 character count (4 bytes LE) + count*2 bytes UCS-2 LE
	int32 Len = Value.Len();
	*this << Len;
	auto Converted = StringCast<UCS2CHAR>(*Value);
	Serialize(const_cast<UCS2CHAR*>(Converted.Get()), Len * 2);
}

FString FSWGPacket::ToString() const
{
	return FString::Printf(
		TEXT("FSWGPacket(Size=%d, Pos=%lld, Encrypted=%d, Compressed=%d)"),
		Data.Num(), Pos, bEncrypted ? 1 : 0, bCompressed ? 1 : 0);
}
