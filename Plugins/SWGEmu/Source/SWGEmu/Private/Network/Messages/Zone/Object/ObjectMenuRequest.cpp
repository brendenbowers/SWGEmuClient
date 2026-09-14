#include "Network/Messages/Zone/Object/ObjectMenuRequest.h"

FObjectMenuRequest::FObjectMenuRequest(uint64 PlayerId, uint64 InTargetId, uint8 InCounter)
	: FObjectControllerMessage(0x146u, PlayerId, 0x0Bu)
	, TargetId(InTargetId)
	, Counter(InCounter)
{}

FSWGPacket FObjectMenuRequest::Serialize() const
{
	FSWGPacket Pkt = SerializeBase(0x05);

	Pkt.WriteUInt32(0); // "size" — parsed and ignored by the server
	Pkt.WriteUInt64(TargetId);
	Pkt.WriteUInt64(ObjectId);
	Pkt.WriteUInt32(ClientItems.Num());
	for (const FSWGRadialMenuEntry& Item : ClientItems)
	{
		Pkt.WriteByte(Item.Index);
		Pkt.WriteByte(Item.ParentIndex);
		Pkt.WriteByte(Item.RadialId);
		Pkt.WriteByte(Item.Callback);
		Pkt.WriteUnicodeString(Item.Text);
	}
	Pkt.WriteByte(Counter);

	return Pkt;
}
