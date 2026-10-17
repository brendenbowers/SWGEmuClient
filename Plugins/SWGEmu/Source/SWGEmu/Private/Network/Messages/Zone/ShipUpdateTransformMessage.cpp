#include "Network/Messages/Zone/ShipUpdateTransformMessage.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/SWGMessageRegistry.h"

REGISTER_SWG_MESSAGE(FShipUpdateTransformMessage, ESWGMessageOp::ShipUpdateTransform)
REGISTER_SWG_MESSAGE(FShipUpdateTransformCollisionMessage, ESWGMessageOp::ShipUpdateTransformCollision)

bool FShipUpdateTransformCollisionMessage::Deserialize(FSWGMessage& Reader)
{
	if (Reader.GetRemaining() != 52) return false;
	ObjectId = Reader.ReadUInt64();
	const float X = Reader.ReadFloat(), Z = Reader.ReadFloat(), Y = Reader.ReadFloat(), W = Reader.ReadFloat();
	Rotation = FQuat(X,Y,Z,W);
	const float Px = Reader.ReadFloat(), Py = Reader.ReadFloat(), Pz = Reader.ReadFloat();
	Position = FVector(Px,Py,Pz) / 4.0958748;
	const float Vx = Reader.ReadFloat(), Vy = Reader.ReadFloat(), Vz = Reader.ReadFloat();
	MovementCounter = Reader.ReadUInt32();
	if (!ObjectId || Rotation.ContainsNaN() || Rotation.SizeSquared() < UE_SMALL_NUMBER
		|| Position.ContainsNaN() || Position.GetAbsMax() > 8000 || FVector(Vx,Vy,Vz).ContainsNaN()) return false;
	Rotation.Normalize();
	return true;
}

bool FShipUpdateTransformMessage::Serialize(FSWGPacket& Out) const
{
	if (Position.ContainsNaN() || Velocity.ContainsNaN() || AngularRates.ContainsNaN()
		|| Rotation.ContainsNaN() || Rotation.SizeSquared() < UE_SMALL_NUMBER
		|| Position.GetAbsMax() > 8000.0) return false;
	FSWGPacket P;
	P.WriteUInt16(8); P.WriteUInt32(uint32(ESWGMessageOp::ShipUpdateTransform)); P.WriteUInt16(ShipId);
	const FQuat Q = Rotation.GetNormalized();
	for (double Value : { Q.W, Q.X, Q.Y, Q.Z }) P.WriteByte(uint8(int8(Value * 127.0)));
	// Retail truncates the product of float position/scale without rounding the intermediate back to float.
	constexpr double PositionScale = double(32767.f / 8000.f);
	for (double Value : { Position.X, Position.Z, Position.Y }) P.WriteInt16(int16(double(float(Value)) * PositionScale));
	const double Speed = Velocity.Size();
	P.WriteInt16(int16(FMath::Min(Speed, 512.0) * (32767.0 / 512.0)));
	const FVector Unit = Speed > 0 ? Velocity / Speed : FVector(0, 0, 1);
	const FVector Abs = Unit.GetAbs();
	const double Scale = 62.0 / (Abs.X + Abs.Y + Abs.Z);
	uint16 PackedDirection = (uint16(Abs.X * Scale) << 6) | uint16(Abs.Y * Scale);
	if (Unit.X < 0) PackedDirection |= 0x8000;
	if (Unit.Y < 0) PackedDirection |= 0x4000;
	if (Unit.Z < 0) PackedDirection |= 0x2000;
	P.WriteUInt16(PackedDirection);
	for (int32 Axis = 0; Axis < 3; ++Axis)
		P.WriteByte(uint8(int8(FMath::Clamp(AngularRates[Axis], -UE_PI/2, UE_PI/2) * (254.0 / UE_PI))));
	P.WriteUInt32(SyncStamp);
	Out = MoveTemp(P);
	return true;
}

bool FShipUpdateTransformMessage::Deserialize(FSWGMessage& Reader)
{
	if (Reader.GetRemaining() != 23) return false;
	ShipId = Reader.ReadUInt16();
	const double W = int8(Reader.ReadByte()) / 127.0;
	const double X = int8(Reader.ReadByte()) / 127.0;
	const double Y = int8(Reader.ReadByte()) / 127.0;
	const double Z = int8(Reader.ReadByte()) / 127.0;
	Rotation = FQuat(X, Y, Z, W);
	const int16 Px = Reader.ReadInt16();
	const int16 Pz = Reader.ReadInt16();
	const int16 Py = Reader.ReadInt16();
	Position = FVector(Px, Py, Pz) * (8000.0 / 32767.0);
	const int16 Speed = Reader.ReadInt16();
	const uint16 Direction = Reader.ReadUInt16();
	const int32 Dx = (Direction >> 6) & 63;
	const int32 Dy = Direction & 63;
	if (Dx + Dy > 63 || (Direction & 0x1000) || Rotation.SizeSquared() < UE_SMALL_NUMBER) return false;
	Rotation.Normalize();
	FVector Unit(Dx, Dy, 63 - Dx - Dy);
	if (Direction & 0x8000) Unit.X = -Unit.X;
	if (Direction & 0x4000) Unit.Y = -Unit.Y;
	if (Direction & 0x2000) Unit.Z = -Unit.Z;
	Velocity = Unit.GetSafeNormal() * Speed * (512.0 / 32767.0);
	const int8 Yaw = int8(Reader.ReadByte());
	const int8 Pitch = int8(Reader.ReadByte());
	const int8 Roll = int8(Reader.ReadByte());
	AngularRates = FVector(Yaw, Pitch, Roll) * (UE_PI / (2.0 * 127.0));
	SyncStamp = Reader.ReadUInt32();
	return true;
}
