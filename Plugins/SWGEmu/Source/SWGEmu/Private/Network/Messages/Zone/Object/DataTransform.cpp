


#include "Network/Messages/Zone/Object/DataTransform.h"

FDataTransform::FDataTransform(uint64 ObjectId)
	: FObjectControllerMessage(0x71u, ObjectId)
{}

FSWGPacket FDataTransform::Serialize() const
{
	FSWGPacket Pkt = SerializeBase(0x10);
	Pkt.WriteUInt32(TimeStamp);
	Pkt.WriteUInt32(MoveCount);
	//uint32 TS = TimeStamp;
	//uint32 Move = MoveCount;

	//Pkt << TS;
	//Pkt << Move;

	// Exact inverse of the incoming FQuat(DirX, DirZ, -DirY, DirW) in
	// USWGObjectGraphSubsystem: the Y/Z swap is a reflection, so the sign has
	// to ride along or the heading comes back mirrored (yaw 0 reads as SWG
	// heading 0 instead of 90, i.e. a quarter turn off in the retail client).
	Pkt.WriteFloat(Direction.X);
	Pkt.WriteFloat(-Direction.Z);
	Pkt.WriteFloat(Direction.Y);
	Pkt.WriteFloat(Direction.W);

	// Core3's Transform::parsePosition reads the wire as X, Z, Y. Position is
	// already in raw SWG units here, but FVector's native X,Y,Z order must not
	// be sent directly or the server treats height as horizontal movement.
	Pkt.WriteFloat(Position.X);
	Pkt.WriteFloat(Position.Z);
	Pkt.WriteFloat(Position.Y);

	Pkt.WriteFloat(Speed);

	//float Spd = Speed;
	//Pkt << Spd;

	return Pkt;
}
