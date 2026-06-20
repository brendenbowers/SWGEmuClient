#include "Network/Messages/SWGMessage.h"

FSWGMessage::FSWGMessage(const FSWGPacket& Packet)
	: Data(Packet.Data), ReadIndex(6), Opcode(PeekOpcode(Packet))
{
}

uint32 FSWGMessage::PeekOpcode(const FSWGPacket& Packet)
{
	if (Packet.Data.Num() < 6)
		return 0;

	uint32 Op;
	FMemory::Memcpy(&Op, Packet.Data.GetData() + 2, sizeof(uint32));
	return NETWORK_ORDER32(Op);
}

int16 FSWGMessage::ReadInt16()
{
	if (ReadIndex + 2 > Data.Num()) return 0;
	int16 Value;
	FMemory::Memcpy(&Value, Data.GetData() + ReadIndex, sizeof(int16));
	ReadIndex += 2;
	return NETWORK_ORDER16(Value);
}

uint16 FSWGMessage::ReadUInt16()
{
	if (ReadIndex + 2 > Data.Num()) return 0;
	uint16 Value;
	FMemory::Memcpy(&Value, Data.GetData() + ReadIndex, sizeof(uint16));
	ReadIndex += 2;
	return NETWORK_ORDER16(Value);
}

int32 FSWGMessage::ReadInt32()
{
	if (ReadIndex + 4 > Data.Num()) return 0;
	int32 Value;
	FMemory::Memcpy(&Value, Data.GetData() + ReadIndex, sizeof(int32));
	ReadIndex += 4;
	return NETWORK_ORDER32(Value);
}

uint32 FSWGMessage::ReadUInt32()
{
	if (ReadIndex + 4 > Data.Num()) return 0;
	uint32 Value;
	FMemory::Memcpy(&Value, Data.GetData() + ReadIndex, sizeof(uint32));
	ReadIndex += 4;
	return NETWORK_ORDER32(Value);
}

int64 FSWGMessage::ReadInt64()
{
	if (ReadIndex + 8 > Data.Num()) return 0;
	int64 Value;
	FMemory::Memcpy(&Value, Data.GetData() + ReadIndex, sizeof(int64));
	ReadIndex += 8;
	return NETWORK_ORDER64(Value);
}

uint64 FSWGMessage::ReadUInt64()
{
	if (ReadIndex + 8 > Data.Num()) return 0;
	uint64 Value;
	FMemory::Memcpy(&Value, Data.GetData() + ReadIndex, sizeof(uint64));
	ReadIndex += 8;
	return NETWORK_ORDER64(Value);
}

float FSWGMessage::ReadFloat()
{
	int32 Raw = ReadInt32();
	float FloatVal;
	FMemory::Memcpy(&FloatVal, &Raw, sizeof(float));
	return FloatVal;
}

FString FSWGMessage::ReadAsciiString()
{
	uint16 Len = ReadUInt16();
	if (ReadIndex + Len > Data.Num()) return FString();

	TArray<ANSICHAR> Buf;
	Buf.SetNum(Len + 1);
	FMemory::Memcpy(Buf.GetData(), Data.GetData() + ReadIndex, Len);
	Buf[Len] = '\0';
	ReadIndex += Len;

	return FString(ANSI_TO_TCHAR(Buf.GetData()));
}

FString FSWGMessage::ReadUnicodeString()
{
	uint16 Len = ReadUInt16();
	if (ReadIndex + (Len * 2) > Data.Num()) return FString();

	FString Result(Len, reinterpret_cast<const TCHAR*>(Data.GetData() + ReadIndex));
	ReadIndex += Len * 2;
	return Result;
}

FString FSWGMessage::ToString() const
{
	return FString::Printf(TEXT("FSWGMessage(Opcode=0x%08X, ReadIdx=%d, Size=%d)"),
		Opcode, ReadIndex, Data.Num());
}
