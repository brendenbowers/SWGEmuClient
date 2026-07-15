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

	// Unreal uses MaxWalkSpeed as the top ground speed for both digital and
	// analog movement. Use SWG's run speed as that ceiling; analog magnitude
	// naturally produces the walk/jog ranges below it, while a full keyboard
	// or stick input reaches the server-provided run speed.
	const float BaselineRunSpeed = RunSpeed * MetersToUnrealUnits;
	const float BaselineWalkSpeed = WalkSpeed * MetersToUnrealUnits;
	MaxWalkSpeed = BaselineRunSpeed > KINDA_SMALL_NUMBER ? BaselineRunSpeed : BaselineWalkSpeed;
	// Same unit conversion as MaxWalkSpeed above — this is missing it would
	// leave MaxAcceleration at ~1 uu/s^2 (the raw multiplier product), so tiny
	// that reaching MaxWalkSpeed from a standstill takes on the order of
	// minutes: the character would visibly only rotate to face movement
	// input (bOrientRotationToMovement doesn't need acceleration) while never
	// actually translating within any reasonable test window.
	const float BaselineAcceleration =
		AccelerationMultiplierBase * AccelerationMultiplierMod * MetersToUnrealUnits;

	// Some Core3 baselines currently send one or both acceleration multipliers
	// as zero. Do not replace the usable constructor default with zero: a
	// CharacterMovementComponent with MaxAcceleration == 0 accepts movement
	// input but can never turn it into velocity.
	if (BaselineAcceleration > KINDA_SMALL_NUMBER)
	{
		MaxAcceleration = BaselineAcceleration;
	}
	RotationRate = FRotator(0.0f, TurnScale, 0.0f);
	SetWalkableFloorAngle(SlopeModAngle);
	MaxSwimSpeed = WaterModPercent * MetersToUnrealUnits;
}
