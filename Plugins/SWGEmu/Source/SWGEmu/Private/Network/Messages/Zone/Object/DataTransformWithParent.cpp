#include "Network/Messages/Zone/Object/DataTransformWithParent.h"

FDataTransformWithParent::FDataTransformWithParent(uint64 ObjectId)
	: FObjectControllerMessage(0xF1u, ObjectId)
{}

FSWGPacket FDataTransformWithParent::Serialize() const
{
	FSWGPacket Pkt = SerializeBase(0x10);
	Pkt.WriteUInt32(TimeStamp);
	Pkt.WriteUInt32(MoveCount);

	Pkt.WriteUInt64(ParentId);

	// Same inverse-of-incoming swap as FDataTransform::Serialize.
	Pkt.WriteFloat(Direction.X);
	Pkt.WriteFloat(-Direction.Z);
	Pkt.WriteFloat(Direction.Y);
	Pkt.WriteFloat(Direction.W);

	// Wire order is X, Z, Y.
	Pkt.WriteFloat(Position.X);
	Pkt.WriteFloat(Position.Z);
	Pkt.WriteFloat(Position.Y);

	Pkt.WriteFloat(Speed);

	return Pkt;
}
