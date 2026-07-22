


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

	Pkt.WriteFloat(Direction.X);
	Pkt.WriteFloat(Direction.Y);
	Pkt.WriteFloat(Direction.Z);
	Pkt.WriteFloat(Direction.W);
	//{
	//	// Same Y/Z-swap convention as Position below and as
	//	// DataTransformMessage.cpp's outgoing Direction (Core3 sends us
	//	// FQuat(DirX, DirZ, DirY, DirW) on SceneObjectCreateMessage — this
	//	// mirrors that swap back out).
	//	float X = Direction.X;
	//	float Y = Direction.Y;
	//	float Z = Direction.Z;
	//	float W = Direction.W;

	//	Pkt << X;
	//	Pkt << Z;
	//	Pkt << Y;
	//	Pkt << W;
	//}

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
