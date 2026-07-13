#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"

/**
 * DataTransform (inner opcode 0x71, wrapped in ObjControllerMessage 0x80CE5E46)
 *
 * The client's own position/movement report — Core3's DataTransformCallback::run()
 * (server/zone/packets/object/DataTransform.h) is the ONLY thing that calls
 * CreatureObject::updateZone(), which is what actually triggers
 * Zone::inRange()'s awareness recompute (GroundZoneComponent::updateZone ->
 * zone->inRange(...)). The initial zone-in teleport() call does this exactly
 * once; a client that never sends its own DataTransform afterward — like a
 * stationary auto-login bot — never triggers a second scan. Confirmed missing
 * entirely from this client (no send site anywhere) while investigating why
 * buildings never appear despite otherwise-correct object-graph handling.
 *
 * Wire layout — matches what the SERVER actually parses
 * (Transform::parseDataTransform, server/zone/packets/object/transform/Transform.h),
 * NOT Core3's client-facing DataTransform.h broadcast-construction class (that one
 * is server->client, one int short of this — mixing the two up here initially
 * silently shifted every field after movementCounter by 4 bytes):
 *   [0x05][0x80CE5E46][0x1B][0x71][objectId(8)][0x00000000]
 *   [timeStamp(4)][movementCounter(4)]
 *   [directionX(4)][directionY(4)][directionZ(4)][directionW(4)]
 *   [posX(4)][posZ(4)][posY(4)]
 *   [speed(4)]
 */
struct SWGEMU_API FDataTransformMessage
{
	int64 ObjectId = 0;
	FVector Position = FVector::ZeroVector;
	FQuat Direction = FQuat::Identity;
	uint32 TimeStamp = 0;
	int32 MovementCounter = 0;
	float Speed = 0.0f;

	FSWGPacket Serialize() const;
};
