


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

	Pkt.WriteFloat(Position.X);
	Pkt.WriteFloat(Position.Y);
	Pkt.WriteFloat(Position.Z);

	//{
	//	// Position wire order is X, Z, Y (Core3's own send order — see
	//	// Transform::parsePosition and DataTransformMessage.cpp's matching
	//	// comment), NOT UE's native X, Y, Z. Sending raw FVector order here
	//	// swapped the server's internal height/horizontal-Y fields on every
	//	// regular movement update (zone-in's FDataTransformMessage already
	//	// had this right; this sibling class — used by
	//	// ASWGPlayer::SendDataTransformUpdate — didn't).
	//	float X = Position.X;
	//	float Y = Position.Y;
	//	float Z = Position.Z;

	//	Pkt << X;
	//	Pkt << Z;
	//	Pkt << Y;
	//}

	Pkt.WriteFloat(Speed);

	//float Spd = Speed;
	//Pkt << Spd;

	return Pkt;
}
