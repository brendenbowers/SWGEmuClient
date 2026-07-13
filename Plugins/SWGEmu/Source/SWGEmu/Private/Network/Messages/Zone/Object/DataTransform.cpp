


#include "Network/Messages/Zone/Object/DataTransform.h"

FDataTransform::FDataTransform(uint64 ObjectId)
	: FObjectControllerMessage(0x71u, ObjectId)
{}

FSWGPacket FDataTransform::Serialize() const
{
	FSWGPacket Pkt = SerializeBase(0x10);

	uint32 TS = TimeStamp;
	uint32 Move = MoveCount;

	Pkt << TS;
	Pkt << Move;
	{
		// Same Y/Z-swap convention as Position below and as
		// DataTransformMessage.cpp's outgoing Direction (Core3 sends us
		// FQuat(DirX, DirZ, DirY, DirW) on SceneObjectCreateMessage — this
		// mirrors that swap back out).
		float X = Direction.X;
		float Y = Direction.Y;
		float Z = Direction.Z;
		float W = Direction.W;

		Pkt << X;
		Pkt << Z;
		Pkt << Y;
		Pkt << W;
	}

	{
		// Position wire order is X, Z, Y (Core3's own send order — see
		// Transform::parsePosition and DataTransformMessage.cpp's matching
		// comment), NOT UE's native X, Y, Z. Sending raw FVector order here
		// swapped the server's internal height/horizontal-Y fields on every
		// regular movement update (zone-in's FDataTransformMessage already
		// had this right; this sibling class — used by
		// ASWGPlayer::SendDataTransformUpdate — didn't).
		float X = Position.X;
		float Y = Position.Y;
		float Z = Position.Z;

		Pkt << X;
		Pkt << Z;
		Pkt << Y;
	}

	float Spd = Speed;
	Pkt << Spd;

	return Pkt;
}
