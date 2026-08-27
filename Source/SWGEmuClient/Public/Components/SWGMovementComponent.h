#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Network/Objects/Zone/Creature/CreatureObjectBaseline.h"
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
class SWGEMUCLIENT_API USWGMovementComponent : public UCharacterMovementComponent
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

	// World time (GetWorld()->GetTimeSeconds()) of the last server position
	// update this creature received — see USWGObjectGraphSubsystem::
	// HandleUpdateTransform, which derives Velocity from the position delta
	// since this timestamp (network-driven actors bypass this movement
	// component's own physics simulation entirely, going straight through
	// SetActorLocation, so Velocity is never otherwise touched for them).
	// -1 means no network update has arrived yet. USWGMeshGeneratorSubsystem::
	// Tick() also reads this to zero out Velocity once updates go stale
	// (the creature stopped and the server simply stops sending updates,
	// rather than sending an explicit zero-speed one), so Velocity doesn't
	// stay frozen mid-stride forever.
	float LastNetworkUpdateTime = -1.0f;

	// Split in three: CREO base4 interleaves these with USWGEncumbranceComponent/
	// USWGSkillComponent/USWGSpaceMissionComponent fields mid-stream — see
	// SWGCreatureBaselineParser::ParseBase4. Part3 recomputes the actual
	// UCharacterMovementComponent properties per the class comment's mapping,
	// once all raw fields are known.
	void ApplyBase4(const FCreatureObjectBaseline& Baseline);
};
