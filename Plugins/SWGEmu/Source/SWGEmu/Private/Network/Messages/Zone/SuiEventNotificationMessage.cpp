#include "Network/Messages/Zone/SuiEventNotificationMessage.h"
#include "Network/Messages/SWGMessageOp.h"

FSWGPacket FSuiEventNotificationMessage::Serialize() const
{
	FSWGPacket Pkt;
	Pkt.WriteUInt16(0x02);
	Pkt.WriteUInt32(static_cast<uint32>(ESWGMessageOp::SuiEventNotification));
	Pkt.WriteUInt32(PageId);
	Pkt.WriteUInt32(EventIndex);
	// Retail writes the argument count twice; the server reads both and loops on the first.
	Pkt.WriteUInt32(Arguments.Num());
	Pkt.WriteUInt32(Arguments.Num());
	for (const FString& Argument : Arguments)
	{
		Pkt.WriteUnicodeString(Argument);
	}
	return Pkt;
}
