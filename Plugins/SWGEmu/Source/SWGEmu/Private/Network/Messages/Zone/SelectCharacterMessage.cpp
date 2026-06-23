#include "Network/Messages/Zone/SelectCharacterMessage.h"
#include "Network/Messages/SWGMessageOp.h"

FSWGPacket FSelectCharacterMessage::Serialize() const
{
	FSWGPacket Pkt;

	uint16 OpcodeCount = 0x02;
	uint32 Opcode      = static_cast<uint32>(ESWGMessageOp::SelectCharacter);
	int64  CharID      = CharacterID;
	uint32 Hash        = SWGEmuHash;

	Pkt << OpcodeCount;
	Pkt << Opcode;
	Pkt << CharID;
	Pkt << Hash;

	return Pkt;
}
