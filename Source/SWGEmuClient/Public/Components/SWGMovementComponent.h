#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Common/SWGPostureTypes.h"
#include "Network/Objects/Zone/Creature/CreatureObjectBaseline.h"
#include "Network/Objects/Zone/Creature/CreatureObjectDelta.h"
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
 *
 * On top of those raw fields, the creature's current posture and states shape
 * the result: datatables/movement/movement_human.iff scales speed/acceleration/
 * turn rate per posture (a prone creature moves at a quarter speed), while
 * movementstates.iff and state_rate_modifiers.iff cap and scale it per active
 * state (Immobilized pins it to zero, Swimming to 70%). See SWGMovementTables.
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
	void ApplyDelta4(const FCreatureObjectDelta& Delta);

	/**
	 * Bound to USWGCombatStateComponent::OnPostureOrStateChanged (see
	 * ASWGCreature::PostInitializeComponents) — re-derives the movement limits
	 * against the new posture/states.
	 */
	void ApplyPostureAndStates(ESWGPosture NewPosture, int64 NewStateBitmask);

	ESWGPosture GetPosture() const { return Posture; }
	int64 GetStateBitmask() const { return StateBitmask; }

	/**
	 * Posture-scaled walk/run speeds in Unreal units — what the animation side
	 * should bucket an observed speed against, since the raw CREO WalkSpeed/
	 * RunSpeed ignore the posture's movementScale entirely.
	 */
	float GetPostureWalkSpeed() const;
	float GetPostureRunSpeed() const;

	/** The locomotion this creature is currently presenting, from its posture and how fast it's actually moving. */
	ESWGLocomotion GetCurrentLocomotion() const;

private:
	void RecomputeMovementLimits();

	ESWGPosture Posture = ESWGPosture::Upright;
	int64 StateBitmask = 0;
};
