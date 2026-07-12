#include "Components/SWGMovementComponent.h"
#include "Network/SWGPacket.h"

namespace
{
	// SWG's RunSpeed/WalkSpeed/water-swim fields are in meters/sec (confirmed
	// empirically: a live packet logged RunSpeed=5.38, WalkSpeed=1.55 — exactly
	// SWG's well-documented real-world human speeds, not raw UE units). UE's
	// convention is 1 unit = 1cm, and nothing before this multiplied up to that
	// — the character was walking at 1.55 cm/sec, imperceptibly slow, which is
	// why the whole world felt absurdly oversized by comparison (not a terrain
	// or mesh scale bug). Position/rotation don't need this — those are already
	// transmitted in UE-matching world units (see FSWGZoneLoadingState::Enter's
	// comment on PositionX/Y/Z).
	constexpr float MetersToUnrealUnits = 100.0f;
}

USWGMovementComponent::USWGMovementComponent()
{
	// MOVE_Flying stands in for MOVE_Walking until real terrain collision
	// exists (see ASWGPlayer::PossessedBy) — but BrakingDecelerationFlying
	// defaults to 0 in the engine (deliberate for drone/ghost-style flight
	// with momentum), which for a walking stand-in means the character never
	// slows down on its own: releasing all movement keys would leave it
	// drifting at whatever velocity it last had, forever. Matching
	// UCharacterMovementComponent's own MOVE_Walking default (2048) makes it
	// stop promptly instead, like an actual walking character would.
	BrakingDecelerationFlying = 2048.0f;
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
