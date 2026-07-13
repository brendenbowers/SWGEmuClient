#include "Components/SWGMovementComponent.h"
#include "Network/SWGPacket.h"

namespace
{
	// SWG's RunSpeed/WalkSpeed/water-swim fields are in meters/sec; UE is 1
	// unit = 1cm. Position/rotation don't need this — those already arrive in
	// UE-matching world units (see FSWGZoneLoadingState::Enter).
	constexpr float MetersToUnrealUnits = 100.0f;
}

USWGMovementComponent::USWGMovementComponent()
{
	// ASWGPlayer::PossessedBy sets MOVE_Walking before the CREO base4 baseline
	// (RunSpeed/WalkSpeed) arrives; movement at UCharacterMovementComponent's
	// stock defaults would trip Core3's speed-hack check. Start conservative;
	// ApplyBase4Part3 overwrites these once the real values are known.
	MaxWalkSpeed = 155.0f;
	MaxAcceleration = 100.0f;
}

void USWGMovementComponent::ApplyBase4Part1(FSWGPacket& Packet)
{
	AccelerationMultiplierBase = Packet.ReadFloat();
	AccelerationMultiplierMod = Packet.ReadFloat();
}

void USWGMovementComponent::ApplyBase4Part2(FSWGPacket& Packet)
{
	SpeedMultiplierBase = Packet.ReadFloat();
	SpeedMultiplierMod = Packet.ReadFloat();
}

void USWGMovementComponent::ApplyBase4Part3(FSWGPacket& Packet)
{
	RunSpeed = Packet.ReadFloat();
	SlopeModAngle = Packet.ReadFloat();
	SlopeModPercent = Packet.ReadFloat();
	TurnScale = Packet.ReadFloat();
	WalkSpeed = Packet.ReadFloat();
	WaterModPercent = Packet.ReadFloat();
	bHasBase4 = true;

	// No run/walk state bit is available in this baseline slot to choose between
	// RunSpeed/WalkSpeed — defaulting to WalkSpeed; a later posture/state update
	// can override this once that bit's source is confirmed.
	MaxWalkSpeed = WalkSpeed * MetersToUnrealUnits;
	// Same unit conversion as MaxWalkSpeed above — this is missing it would
	// leave MaxAcceleration at ~1 uu/s^2 (the raw multiplier product), so tiny
	// that reaching MaxWalkSpeed from a standstill takes on the order of
	// minutes: the character would visibly only rotate to face movement
	// input (bOrientRotationToMovement doesn't need acceleration) while never
	// actually translating within any reasonable test window.
	MaxAcceleration = AccelerationMultiplierBase * AccelerationMultiplierMod * MetersToUnrealUnits;
	RotationRate = FRotator(0.0f, TurnScale, 0.0f);
	SetWalkableFloorAngle(SlopeModAngle);
	MaxSwimSpeed = WaterModPercent * MetersToUnrealUnits;
}
