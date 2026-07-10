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
	MaxAcceleration = AccelerationMultiplierBase * AccelerationMultiplierMod;
	RotationRate = FRotator(0.0f, TurnScale, 0.0f);
	SetWalkableFloorAngle(SlopeModAngle);
	MaxSwimSpeed = WaterModPercent * MetersToUnrealUnits;
}
