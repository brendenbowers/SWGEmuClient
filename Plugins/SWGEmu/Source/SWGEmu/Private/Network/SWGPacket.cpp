#include "Network/SWGPacket.h"

FSWGPacket::FSWGPacket()
	: Data()
{
	Data.SetNumZeroed(496);
}

FSWGPacket::FSWGPacket(int32 Size)
	: Data()
{
	Data.SetNumZeroed(Size);
}

FSWGPacket::FSWGPacket(const uint8* Bytes, int32 Len)
	: Data(Bytes, Len), WriteIndex(Len)
{
}

void FSWGPacket::SetSize(int32 NewSize)
{
	Data.SetNum(NewSize);
	if (ReadIndex > NewSize) ReadIndex = NewSize;
	if (WriteIndex > NewSize) WriteIndex = NewSize;
}

void FSWGPacket::ShrinkToWriteIndex()
{
	Data.SetNum(WriteIndex);
}

void FSWGPacket::Reset()
{
	ReadIndex = 0;
	WriteIndex = 0;
	bEncrypted = false;
	bCompressed = false;
	CRC = 0;
	TimeCreated = 0;
	TimeSent = 0;
	Resends = 0;
}

// ──────────────────────────────────────────────────────────────
// Read Helpers
// ──────────────────────────────────────────────────────────────

uint8 FSWGPacket::ReadByte()
{
	return (ReadIndex < Data.Num()) ? Data[ReadIndex++] : 0;
}

int16 FSWGPacket::ReadInt16()
{
	if (ReadIndex + 2 > Data.Num()) return 0;
	int16 Value;
	FMemory::Memcpy(&Value, Data.GetData() + ReadIndex, sizeof(int16));
	ReadIndex += 2;
	return NETWORK_ORDER16(Value);
}

uint16 FSWGPacket::ReadUInt16()
{
	if (ReadIndex + 2 > Data.Num()) return 0;
	uint16 Value;
	FMemory::Memcpy(&Value, Data.GetData() + ReadIndex, sizeof(uint16));
	ReadIndex += 2;
	return NETWORK_ORDER16(Value);
}

int32 FSWGPacket::ReadInt32()
{
	if (ReadIndex + 4 > Data.Num()) return 0;
	int32 Value;
	FMemory::Memcpy(&Value, Data.GetData() + ReadIndex, sizeof(int32));
	ReadIndex += 4;
	return NETWORK_ORDER32(Value);
}

uint32 FSWGPacket::ReadUInt32()
{
	if (ReadIndex + 4 > Data.Num()) return 0;
	uint32 Value;
	FMemory::Memcpy(&Value, Data.GetData() + ReadIndex, sizeof(uint32));
	ReadIndex += 4;
	return NETWORK_ORDER32(Value);
}

int64 FSWGPacket::ReadInt64()
{
	if (ReadIndex + 8 > Data.Num()) return 0;
	int64 Value;
	FMemory::Memcpy(&Value, Data.GetData() + ReadIndex, sizeof(int64));
	ReadIndex += 8;
	return NETWORK_ORDER64(Value);
}

uint64 FSWGPacket::ReadUInt64()
{
	if (ReadIndex + 8 > Data.Num()) return 0;
	uint64 Value;
	FMemory::Memcpy(&Value, Data.GetData() + ReadIndex, sizeof(uint64));
	ReadIndex += 8;
	return NETWORK_ORDER64(Value);
}

float FSWGPacket::ReadFloat()
{
	int32 Raw = ReadInt32();
	float FloatVal;
	FMemory::Memcpy(&FloatVal, &Raw, sizeof(float));
	return FloatVal;
}

FString FSWGPacket::ReadAsciiString()
{
	// Pascal-style: uint16 length + characters (no null terminator)
	uint16 Len = ReadUInt16();
	if (ReadIndex + Len > Data.Num()) return FString();

	TArray<ANSICHAR> Buf;
	Buf.SetNum(Len + 1);
	FMemory::Memcpy(Buf.GetData(), Data.GetData() + ReadIndex, Len);
	Buf[Len] = '\0';
	ReadIndex += Len;

	return FString(ANSI_TO_TCHAR(Buf.GetData()));
}

FString FSWGPacket::ReadUnicodeString()
{
	// Pascal-style: uint16 length + UTF-16 characters
	uint16 Len = ReadUInt16();
	if (ReadIndex + (Len * 2) > Data.Num()) return FString();

	FString Result(Len, reinterpret_cast<const TCHAR*>(Data.GetData() + ReadIndex));
	ReadIndex += Len * 2;
	return Result;
}

void FSWGPacket::Skip(int32 NumBytes)
{
	ReadIndex += NumBytes;
	if (ReadIndex > Data.Num()) ReadIndex = Data.Num();
}

// ──────────────────────────────────────────────────────────────
// Write Helpers
// ──────────────────────────────────────────────────────────────

void FSWGPacket::WriteByte(uint8 Value)
{
	EnsureCapacity(1);
	Data[WriteIndex++] = Value;
}

void FSWGPacket::WriteInt16(int16 Value)
{
	Value = NETWORK_ORDER16(Value);
	EnsureCapacity(2);
	FMemory::Memcpy(Data.GetData() + WriteIndex, &Value, sizeof(int16));
	WriteIndex += 2;
}

void FSWGPacket::WriteUInt16(uint16 Value)
{
	Value = NETWORK_ORDER16(Value);
	EnsureCapacity(2);
	FMemory::Memcpy(Data.GetData() + WriteIndex, &Value, sizeof(uint16));
	WriteIndex += 2;
}

void FSWGPacket::WriteInt32(int32 Value)
{
	Value = NETWORK_ORDER32(Value);
	EnsureCapacity(4);
	FMemory::Memcpy(Data.GetData() + WriteIndex, &Value, sizeof(int32));
	WriteIndex += 4;
}

void FSWGPacket::WriteUInt32(uint32 Value)
{
	Value = NETWORK_ORDER32(Value);
	EnsureCapacity(4);
	FMemory::Memcpy(Data.GetData() + WriteIndex, &Value, sizeof(uint32));
	WriteIndex += 4;
}

void FSWGPacket::WriteInt64(int64 Value)
{
	Value = NETWORK_ORDER64(Value);
	EnsureCapacity(8);
	FMemory::Memcpy(Data.GetData() + WriteIndex, &Value, sizeof(int64));
	WriteIndex += 8;
}

void FSWGPacket::WriteUInt64(uint64 Value)
{
	Value = NETWORK_ORDER64(Value);
	EnsureCapacity(8);
	FMemory::Memcpy(Data.GetData() + WriteIndex, &Value, sizeof(uint64));
	WriteIndex += 8;
}

void FSWGPacket::WriteFloat(float Value)
{
	int32 Raw;
	FMemory::Memcpy(&Raw, &Value, sizeof(float));
	WriteInt32(Raw);
}

void FSWGPacket::WriteAsciiString(const FString& Value)
{
	FTCHARToUTF8 Converted(*Value);
	int32 Len = Converted.Length();
	WriteUInt16((uint16)Len);
	EnsureCapacity(Len);
	FMemory::Memcpy(Data.GetData() + WriteIndex, (const uint8*)Converted.Get(), Len);
	WriteIndex += Len;
}

void FSWGPacket::WriteUnicodeString(const FString& Value)
{
	int32 Len = Value.Len();
	WriteUInt16((uint16)Len);
	EnsureCapacity(Len * 2);
	FMemory::Memcpy(Data.GetData() + WriteIndex, *Value, Len * 2);
	WriteIndex += Len * 2;
}

FString FSWGPacket::ToString() const
{
	return FString::Printf(TEXT("FSWGPacket(Size=%d, ReadIdx=%d, WriteIdx=%d, Encrypted=%d, Compressed=%d)"),
		Data.Num(), ReadIndex, WriteIndex, bEncrypted ? 1 : 0, bCompressed ? 1 : 0);
}

void FSWGPacket::EnsureCapacity(int32 NumBytesNeeded)
{
	if (WriteIndex + NumBytesNeeded > Data.Num())
	{
		Data.SetNum(WriteIndex + NumBytesNeeded + 64);
	}
}
