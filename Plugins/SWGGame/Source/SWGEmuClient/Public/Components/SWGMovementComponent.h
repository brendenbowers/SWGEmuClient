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
 *   TurnScale                 -> RotationRate (a multiplier on the template's turnRate)
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

	/**
	 * Walks the actor toward NetworkTargetLocation when one is set, skipping
	 * Super: network-driven creatures still default to MOVE_Walking, so
	 * PerformMovement would simulate them anyway and fight both the position
	 * set here and the Velocity the blend space reads. With no target the
	 * inherited tick runs as normal, keeping idle creatures on the floor.
	 */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

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
	// -1 means no network update has arrived yet.
	float LastNetworkUpdateTime = -1.0f;

	/**
	 * Server-authoritative pose this actor is easing toward. Set by
	 * USWGObjectGraphSubsystem::HandleUpdateTransform, consumed by this tick.
	 * Unset means arrived, or the update was a teleport and got applied
	 * directly. Never set for the locally controlled pawn, whose corrections
	 * must land immediately.
	 */
	TOptional<FVector> NetworkTargetLocation;
	TOptional<float> NetworkTargetYaw;

	void SetNetworkTarget(const FVector& Location, float Yaw)
	{
		NetworkTargetLocation = Location;
		NetworkTargetYaw = Yaw;
	}

	void ClearNetworkTarget()
	{
		NetworkTargetLocation.Reset();
		NetworkTargetYaw.Reset();
	}

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

	/**
	 * The creature template's "turnRate" (degrees/second, run and walk) —
	 * what CREO4 TurnScale multiplies. Set once the template is known; see
	 * USWGObjectGraphSubsystem's creature spawn.
	 */
	void SetTemplateTurnRates(float RunTurnRate, float WalkTurnRate);
	float GetTemplateRunTurnRate() const { return TemplateRunTurnRate; }

private:
	void RecomputeMovementLimits();

	// shared_base_player.iff's turnRate, which every player template inherits —
	// the rate until the actual template's arrives.
	float TemplateRunTurnRate = 720.0f;
	float TemplateWalkTurnRate = 720.0f;

	/** One frame of easing toward NetworkTargetLocation/Yaw. Sets Velocity from the step actually taken, so the blend space animates the movement. */
	void TickNetworkSmoothing(float DeltaTime);

	/** Roughly one server update interval — the gap is spread over this long, so the actor arrives as the next update lands. */
	static constexpr float NetworkSmoothingTime = 0.3f;
	/** Headroom over the creature's run speed, to make up a gap after a network hitch. */
	static constexpr float NetworkCatchUpSpeedTolerance = 1.5f;
	/** Within this horizontal distance the actor snaps to the target and stops, so it idles instead of creeping. */
	static constexpr float NetworkArrivalTolerance = 2.0f;
	/** Height chases faster than the horizontal walk — terrain can climb faster than a creature crosses it. */
	static constexpr float NetworkHeightInterpSpeed = 10.0f;
	/** Turn rate toward the server's heading. */
	static constexpr float NetworkYawInterpSpeed = 8.0f;

	ESWGPosture Posture = ESWGPosture::Upright;
	int64 StateBitmask = 0;
};
