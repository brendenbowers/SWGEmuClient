#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SWGMovementComponent.generated.h"

struct FSWGPacket;

/**
 * CREO base4 movement fields, applied directly onto UCharacterMovementComponent's
 * own properties rather than duplicating them:
 *   RunSpeed/WalkSpeed        -> MaxWalkSpeed (swapped by current run/walk state)
 *   TurnScale                 -> RotationRate
 *   SlopeModAngle             -> SetWalkableFloorAngle()
 *   AccelerationMultiplier*   -> MaxAcceleration
 *   WaterModPercent           -> MaxSwimSpeed
 *
 * Attach via ACharacter's ObjectInitializer (SetDefaultSubobjectClass) so this
 * class IS the character's movement component, not a second component alongside it.
 */
UCLASS()
class SWGEMU_API USWGMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	USWGMovementComponent();

	float AccelerationMultiplierBase = 0.f;
	float AccelerationMultiplierMod  = 0.f;
	float SpeedMultiplierBase = 0.f;
	float SpeedMultiplierMod  = 0.f;
	float RunSpeed        = 0.f;
	float SlopeModAngle   = 0.f;
	float SlopeModPercent = 0.f;
	float TurnScale       = 0.f;
	float WalkSpeed       = 0.f;
	float WaterModPercent = 0.f;
	bool  bHasBase4 = false;

	// Split in three: CREO base4 interleaves these with USWGEncumbranceComponent/
	// USWGSkillComponent/USWGSpaceMissionComponent fields mid-stream — see
	// SWGCreatureBaselineParser::ParseBase4. Part3 recomputes the actual
	// UCharacterMovementComponent properties per the class comment's mapping,
	// once all raw fields are known.
	void ApplyBase4Part1(FSWGPacket& Packet); // AccelerationMultiplierBase, AccelerationMultiplierMod
	void ApplyBase4Part2(FSWGPacket& Packet); // SpeedMultiplierBase, SpeedMultiplierMod
	void ApplyBase4Part3(FSWGPacket& Packet); // RunSpeed..WaterModPercent
};
