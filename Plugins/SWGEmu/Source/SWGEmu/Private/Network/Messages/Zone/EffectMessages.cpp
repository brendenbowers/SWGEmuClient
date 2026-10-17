#include "Network/Messages/Zone/EffectMessages.h"
#include "Network/Messages/SWGMessage.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/SWGMessageRegistry.h"

REGISTER_SWG_MESSAGE(FPlayMusicMessage, ESWGMessageOp::PlayMusicMessage)
REGISTER_SWG_MESSAGE(FPlayClientEffectObjectMessage, ESWGMessageOp::PlayClientEffectObjectMessage)
REGISTER_SWG_MESSAGE(FPlayClientEffectObjectTransformMessage, ESWGMessageOp::PlayClientEffectObjectTransformMessage)
REGISTER_SWG_MESSAGE(FPlayClientEffectLocMessage, ESWGMessageOp::PlayClientEffectLoc)
REGISTER_SWG_MESSAGE(FPlayClientEventObjectMessage, ESWGMessageOp::PlayClientEventObjectMessage)

bool FPlayMusicMessage::Deserialize(FSWGMessage& Reader)
{
	SoundFile = Reader.ReadAsciiString();
	Reader >> Target >> UnknownInt >> UnknownBool;
	return true;
}

bool FPlayClientEffectObjectMessage::Deserialize(FSWGMessage& Reader)
{
	EffectFile = Reader.ReadAsciiString();
	Hardpoint = Reader.ReadAsciiString();
	Reader >> ObjectId;
	return true;
}

bool FPlayClientEffectObjectTransformMessage::Deserialize(FSWGMessage& Reader)
{
	if (Reader.GetRemaining() < 2) return false;
	EffectFile = Reader.ReadAsciiString();
	if (EffectFile.IsEmpty() || Reader.GetRemaining() != 36) return false;
	Reader >> RotationX >> RotationY >> RotationZ >> RotationW >> X >> Y >> Z >> ObjectId;
	const FQuat Rotation(RotationX, RotationY, RotationZ, RotationW);
	return !Rotation.ContainsNaN() && FMath::IsFinite(Rotation.SizeSquared())
		&& Rotation.SizeSquared() > UE_SMALL_NUMBER && !FVector(X, Y, Z).ContainsNaN();
}

bool FPlayClientEffectLocMessage::Deserialize(FSWGMessage& Reader)
{
	EffectFile = Reader.ReadAsciiString();
	Planet = Reader.ReadAsciiString();
	Reader >> X >> Z >> Y >> CellId >> TerrainDelta;
	Label = Reader.ReadAsciiString();
	return true;
}

bool FPlayClientEventObjectMessage::Deserialize(FSWGMessage& Reader)
{
	EventName = Reader.ReadAsciiString();
	Aux = Reader.ReadAsciiString();
	Reader >> ObjectId;
	return true;
}
