#include "Network/SWGPacket.h"

// ── Constructors ──────────────────────────────────────────────────────────────

FSWGPacket::FSWGPacket()
{
	SetIsSaving(true);
	ArForceByteSwapping = true; // SOE wire format is big-endian; operator<< uses this
}

FSWGPacket::FSWGPacket(const uint8* Bytes, int32 Len)
{
	Data.Append(Bytes, Len);
	SetIsLoading(true);
	ArForceByteSwapping = true;
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
		if (Pos + ByteLen > (int64)Data.Num())
			Data.SetNum((int32)(Pos + ByteLen) + 64);
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
	uint16 Len = 0;
	*this << Len;
	if (IsError() || Pos + (int64)(Len * 2) > (int64)Data.Num())
	{
		SetError();
		return FString();
	}

	FString Result(Len, reinterpret_cast<const TCHAR*>(Data.GetData() + Pos));
	Pos += Len * 2;
	return Result;
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
	FTCHARToUTF8 Converted(*Value);
	const int32 Len = Converted.Length();
	uint16 Len16 = (uint16)Len;
	*this << Len16; // big-endian length
	Serialize(const_cast<ANSICHAR*>(Converted.Get()), Len);
}

void FSWGPacket::WriteUnicodeString(const FString& Value)
{
	const int32 Len = Value.Len();
	uint16 Len16 = (uint16)Len;
	*this << Len16;
	// TCHAR may be 2 or 4 bytes depending on platform; SOE expects UTF-16 (2-byte)
	Serialize(const_cast<TCHAR*>(*Value), Len * 2);
}

FString FSWGPacket::ToString() const
{
	return FString::Printf(
		TEXT("FSWGPacket(Size=%d, Pos=%lld, Encrypted=%d, Compressed=%d)"),
		Data.Num(), Pos, bEncrypted ? 1 : 0, bCompressed ? 1 : 0);
}
