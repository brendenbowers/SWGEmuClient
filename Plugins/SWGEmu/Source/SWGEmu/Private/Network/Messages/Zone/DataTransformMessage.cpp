#include "Network/Messages/Zone/DataTransformMessage.h"

FSWGPacket FDataTransformMessage::Serialize() const
{
	FSWGPacket Pkt;

	// ObjectControllerMessage envelope — matches Core3's
	// ObjectControllerMessage(objid, 0x1B, 0x71) constructor exactly.
	Pkt.WriteUInt16(0x05);
	Pkt.WriteUInt32(0x80CE5E46u); // ObjControllerMessage
	Pkt.WriteUInt32(0x1B);        // header1
	Pkt.WriteUInt32(0x71);        // DataTransform sub-opcode
	Pkt.WriteInt64(ObjectId);

	// NOTE: Core3's own ObjectControllerMessage constructor writes a trailing
	// insertInt(0x00) here, but ObjectControllerMessageCallback::parse (the
	// server-side handler) only ever consumes priority(4)+type(4)+objectID(8)
	// before handing off to the sub-callback's own parse() — it never reads
	// that trailing int. Sending it shifted every field of the payload by one
	// (confirmed via TRANSFORM_DEBUG: our padding was being read back as
	// "Transform Timestamp: 0"). Omit it entirely.

	// DataTransform's own payload — timeStamp THEN moveCount (Transform::
	// parseDataTransform reads both, in that order; missing the timestamp here
	// was the original bug, shifting every field after it by 4 bytes).
	Pkt.WriteUInt32(TimeStamp);
	Pkt.WriteInt32(MovementCounter);

	// Same Y/Z-swap convention as the rotation quaternion Core3 sends us on
	// SceneObjectCreateMessage (HandleSceneCreateObject builds
	// FQuat(Msg.DirX, Msg.DirZ, Msg.DirY, Msg.DirW) from it) — inverted here
	// for the outgoing direction.
	Pkt.WriteFloat(Direction.X);
	Pkt.WriteFloat(Direction.Z);
	Pkt.WriteFloat(Direction.Y);
	Pkt.WriteFloat(Direction.W);

	// Position wire order is X, Z, Y (Core3's own send order — no axis
	// remapping needed, PositionZ already is SWG's vertical axis, matching
	// FSWGZoneLoadingState::Enter's confirmed position convention).
	Pkt.WriteFloat(Position.X);
	Pkt.WriteFloat(Position.Z);
	Pkt.WriteFloat(Position.Y);

	Pkt.WriteFloat(Speed);

	return Pkt;
}
