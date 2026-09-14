#include "Network/Messages/Zone/Object/ObjectMenuResponseIn.h"
#include "Network/SWGPacket.h"

bool FObjectMenuResponseIn::Parse(FSWGPacket& Packet)
{
	TargetId = Packet.ReadUInt64();
	PlayerId = Packet.ReadUInt64();

	const uint32 Count = Packet.ReadUInt32();
	if (Packet.IsError() || Count > 255)
	{
		return false;
	}

	Items.Reset(Count);
	for (uint32 ItemIndex = 0; ItemIndex < Count && !Packet.IsError(); ++ItemIndex)
	{
		FSWGRadialMenuEntry& Item = Items.AddDefaulted_GetRef();
		Item.Index = Packet.ReadByte();
		Item.ParentIndex = Packet.ReadByte();
		Item.RadialId = Packet.ReadByte();
		Item.Callback = Packet.ReadByte();
		Item.Text = Packet.ReadUnicodeString();
	}
	Counter = Packet.ReadByte();

	return !Packet.IsError();
}
