#include "Network/Messages/SWGMessage.h"
#include "Containers/StringConv.h"

namespace {
	template<typename TCount>
	void WriteCharacterArray(const ANSICHAR* Value, TCount Count, FSWGPacket& Packet)
	{
		Packet << Count;
		if (Count > 0) {
			Packet.Serialize(const_cast<ANSICHAR*>(Value), Count);
		}
	}
}

FSWGMessage::FSWGMessage(const FSWGPacket& InPacket)
	: Packet(InPacket)
{
	// Read opcode before setting cursor position.
	Opcode = PeekOpcode(Packet);

	// Position cursor at payload start: skip 2-byte session sub-type + 4-byte opcode.
	Packet.SetReadMode();
	Packet.Skip(6);
}

uint32 FSWGMessage::PeekOpcode(const FSWGPacket& Pkt)
{
	if (Pkt.Data.Num() < 6)
		return 0;

	// Read 4 bytes at offset 2 through a temporary packet so FArchive handles
	// the big-endian conversion — no NETWORK_ORDER macros needed.
	FSWGPacket Reader(Pkt.Data.GetData() + 2, 4);
	return Reader.ReadUInt32();
}

// ── Read helpers — all delegate to Packet ────────────────────────────────────

uint8   FSWGMessage::ReadByte()          { return Packet.ReadByte(); }
int16   FSWGMessage::ReadInt16()         { return Packet.ReadInt16(); }
uint16  FSWGMessage::ReadUInt16()        { return Packet.ReadUInt16(); }
int32   FSWGMessage::ReadInt32()         { return Packet.ReadInt32(); }
uint32  FSWGMessage::ReadUInt32()        { return Packet.ReadUInt32(); }
int64   FSWGMessage::ReadInt64()         { return Packet.ReadInt64(); }
uint64  FSWGMessage::ReadUInt64()        { return Packet.ReadUInt64(); }
float   FSWGMessage::ReadFloat()         { return Packet.ReadFloat(); }
FString FSWGMessage::ReadAsciiString()   { return Packet.ReadAsciiString(); }
FString FSWGMessage::ReadUnicodeString() { return Packet.ReadUnicodeString(); }
void    FSWGMessage::Skip(int32 N)       { Packet.Skip(N); }
void    FSWGMessage::Serialize(void* V, int64 Length)
{
	Packet.Serialize(V, Length);
}

void FSWGMessage::WriteAsciiString(const FString& Value)
{
	auto Converter = StringCast<ANSICHAR>(*Value);
	int16 Length = Converter.Length();

	WriteCharacterArray(Converter.Get(), Length, Packet);
}

void FSWGMessage::WriteUnicodeString(const FString& Value)
{
	FTCHARToUTF8 Converter(*Value);
	int32 Length = Converter.Length();
	WriteCharacterArray(Converter.Get(), Length, Packet);
}

void FSWGMessage::WriteAsciiString(const FText& Value)
{
	auto Converter = StringCast<ANSICHAR>(*Value.ToString());
	int16 Length = Converter.Length();

	WriteCharacterArray(Converter.Get(), Length, Packet);
}

void FSWGMessage::WriteUnicodeString(const FText& Value)
{
	FTCHARToUTF8 Converter(*Value.ToString());
	int32 Length = Converter.Length();
	WriteCharacterArray(Converter.Get(), Length, Packet);
}


int32   FSWGMessage::GetRemaining() const { return Packet.GetRemaining(); }
bool    FSWGMessage::IsAtEnd() const      { return Packet.IsAtEnd(); }

FString FSWGMessage::ToString() const
{
	return FString::Printf(TEXT("FSWGMessage(Opcode=0x%08X, Remaining=%d, Size=%d)"),
		Opcode, GetRemaining(), Packet.GetSize());
}

void FSWGMessage::Seek(int32 Position)
{
	Packet.Seek(Position);
}
