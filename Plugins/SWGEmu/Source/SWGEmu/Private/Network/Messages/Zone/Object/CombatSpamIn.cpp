#include "Network/Messages/Zone/Object/CombatSpamIn.h"
#include "Network/SWGPacket.h"

bool FCombatSpamIn::Parse(FSWGPacket& Packet)
{
	AttackerId = Packet.ReadInt64();
	DefenderId = Packet.ReadInt64();
	ItemId     = Packet.ReadInt64();
	Damage     = Packet.ReadInt32();

	StringFile = Packet.ReadAsciiString();
	Packet.ReadUInt32(); // padding — the usual shape of a StringId on the wire
	StringName = Packet.ReadAsciiString();

	Color      = Packet.ReadByte();
	CustomText = Packet.ReadUnicodeString();

	return !Packet.IsError();
}

FString FCombatSpamIn::GetStringId() const
{
	if (StringFile.IsEmpty() || StringName.IsEmpty())
	{
		return FString();
	}

	return FString::Printf(TEXT("@%s:%s"), *StringFile, *StringName);
}
