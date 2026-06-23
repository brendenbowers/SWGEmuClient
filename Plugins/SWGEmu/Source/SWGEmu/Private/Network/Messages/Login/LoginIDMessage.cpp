#include "Network/Messages/Login/LoginIDMessage.h"
#include "Network/Messages/SWGMessageOp.h"

const FString FLoginIDMessage::ClientVersion = TEXT("20050408-18:00");

FSWGPacket FLoginIDMessage::Serialize() const
{
	FSWGPacket Pkt;

	uint16 OpcodeCount = 0x04;
	uint32 Opcode      = static_cast<uint32>(ESWGMessageOp::LoginClientID);

	Pkt << OpcodeCount;
	Pkt << Opcode;
	Pkt.WriteAsciiString(Username);
	Pkt.WriteAsciiString(Password);
	Pkt.WriteAsciiString(ClientVersion);

	return Pkt;
}
