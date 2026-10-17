#pragma once
#include "CoreMinimal.h"
#include "Network/Messages/SWGNetMessage.h"
#include "Network/SWGPacket.h"

/** Core3 ship movement: raw SWG axes/metres, not Unreal coordinates.
 * Server ShipId is the SHIP baseline-6 identifier; retail client sends zero. */
struct SWGEMU_API FShipUpdateTransformMessage : FSWGNetMessage
{
	uint16 ShipId = 0;
	FQuat Rotation = FQuat::Identity;
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector AngularRates = FVector::ZeroVector; // yaw, pitch, roll; radians/second
	uint32 SyncStamp = 0;
	bool bValid = false;
	FShipUpdateTransformMessage() = default;
	FShipUpdateTransformMessage(uint32 Op, FSWGMessage& Reader) : FSWGNetMessage(Op, Reader) { bValid = Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
	bool Serialize(FSWGPacket& Out) const;
};

/** Core3 collision correction uses an object ID and full floats, not the compact movement codec. */
struct SWGEMU_API FShipUpdateTransformCollisionMessage : FSWGNetMessage
{
	uint64 ObjectId = 0;
	FQuat Rotation = FQuat::Identity;
	FVector Position = FVector::ZeroVector;
	uint32 MovementCounter = 0;
	bool bValid = false;
	FShipUpdateTransformCollisionMessage(uint32 Op, FSWGMessage& Reader) : FSWGNetMessage(Op, Reader) { bValid = Deserialize(Reader); }
	bool Deserialize(FSWGMessage& Reader);
};
