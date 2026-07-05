#include "Components/SWGMovementComponent.h"

USWGMovementComponent::USWGMovementComponent()
{
}

void USWGMovementComponent::ApplyBase4(FSWGPacket& Packet)
{
	// TODO: decode CREO base4 fields, then recompute MaxWalkSpeed/MaxAcceleration/
	// RotationRate/WalkableFloorAngle from them (see class comment for the mapping).
}
