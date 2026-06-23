#include "Network/Messages/Zone/ClientIdMessage.h"
#include "Network/Messages/SWGMessageOp.h"

const FString FClientIdMessage::DefaultClientVersion = TEXT("20050408-18:00");

FSWGPacket FClientIdMessage::Serialize() const
{
	FSWGPacket Pkt;

	uint16 OpcodeCount = 0x03;
	uint32 Opcode      = static_cast<uint32>(ESWGMessageOp::ClientIdMsg);
	int32  GameBits    = 0xFE;
	int32  BlobSize    = SessionKey.Num();

	Pkt << OpcodeCount;
	Pkt << Opcode;
	Pkt << GameBits;
	Pkt << BlobSize;
	Pkt.Serialize(const_cast<uint8*>(SessionKey.GetData()), SessionKey.Num());
	Pkt.WriteAsciiString(ClientVersion.IsEmpty() ? DefaultClientVersion : ClientVersion);

	return Pkt;
}
