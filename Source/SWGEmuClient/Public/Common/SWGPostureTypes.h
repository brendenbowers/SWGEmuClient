#pragma once

#include "CoreMinimal.h"
#include "SWGPostureTypes.generated.h"

/**
 * Values are the "value" column of datatables/include/posture.iff, which is
 * also what CREO base3's Posture byte carries on the wire. The datatable
 * spells Invalid as -1; the server's CreaturePosture enum (templates/params/
 * creature/CreaturePosture.h) widens that to 0xFF for the same unsigned byte,
 * so that's what we use here too.
 */
UENUM(BlueprintType)
enum class ESWGPosture : uint8
{
	Upright        = 0,
	Crouched       = 1,
	Prone          = 2,
	Sneaking       = 3,
	Blocking       = 4,
	Climbing       = 5,
	Flying         = 6,
	LyingDown      = 7,
	Sitting        = 8,
	SkillAnimating = 9,
	DrivingVehicle = 10,
	RidingCreature = 11,
	KnockedDown    = 12,
	Incapacitated  = 13,
	Dead           = 14,
	Invalid        = 255
};

/**
 * Values are the "value" column of datatables/include/locomotion.iff. A
 * locomotion is the *observable* movement mode a creature is in — the join of
 * its posture and how fast it is actually moving — and is what the animation
 * side keys off. Resolved from posture + speed category through
 * datatables/movement/movement_human.iff; see SWGMovementTables.
 */
UENUM(BlueprintType)
enum class ESWGLocomotion : uint8
{
	Standing           = 0,
	Sneaking           = 1,
	Walking            = 2,
	Running            = 3,
	Kneeling           = 4,
	CrouchSneaking     = 5,
	CrouchWalking      = 6,
	Prone              = 7,
	Crawling           = 8,
	ClimbingStationary = 9,
	Climbing           = 10,
	Hovering           = 11,
	Flying             = 12,
	LyingDown          = 13,
	Sitting            = 14,
	SkillAnimating     = 15,
	DrivingVehicle     = 16,
	RidingCreature     = 17,
	KnockedDown        = 18,
	Incapacitated      = 19,
	Dead               = 20,
	Blocking           = 21,
	Invalid            = 255
};

/**
 * Values are the "value" column of datatables/include/state.iff — a *bit
 * index*, not a mask. CREO base3's StateBitmask sets bit (1 << value) per
 * active state, matching the server's CreatureState enum, which spells the
 * same table out as 0x01/0x02/0x04/... PilotingPobShip at index 33 is why the
 * bitmask is an int64 rather than an int32.
 */
UENUM(BlueprintType)
enum class ESWGState : uint8
{
	Cover                    = 0,
	Combat                   = 1,
	Peace                    = 2,
	Aiming                   = 3,
	Alert                    = 4,
	Berserk                  = 5,
	FeignDeath               = 6,
	CombatAttitudeEvasive    = 7,
	CombatAttitudeNormal     = 8,
	CombatAttitudeAggressive = 9,
	Tumbling                 = 10,
	Rallied                  = 11,
	Stunned                  = 12,
	Blinded                  = 13,
	Dizzy                    = 14,
	Intimidated              = 15,
	Immobilized              = 16,
	Frozen                   = 17,
	Swimming                 = 18,
	SittingOnChair           = 19,
	Crafting                 = 20,
	GlowingJedi              = 21,
	MaskScent                = 22,
	Poisoned                 = 23,
	Bleeding                 = 24,
	Diseased                 = 25,
	OnFire                   = 26,
	RidingMount              = 27,
	MountedCreature          = 28,
	PilotingShip             = 29,
	ShipOperations           = 30,
	ShipGunner               = 31,
	ShipInterior             = 32,
	PilotingPobShip          = 33,
	Invalid                  = 255
};

/**
 * The three movement speed buckets a locomotion is picked from — the
 * "stationary"/"slow"/"fast" columns of datatables/movement/movement_human.iff
 * and the values of movementstates.iff's maxSpeedCategory enum column.
 */
UENUM(BlueprintType)
enum class ESWGSpeedCategory : uint8
{
	Stationary = 0,
	Slow       = 1,
	Fast       = 2
};

/** Bit for one state within a CREO base3 StateBitmask. */
FORCEINLINE int64 SWGStateBit(ESWGState State)
{
	return State == ESWGState::Invalid ? 0 : (int64(1) << int64((uint8)State));
}

FORCEINLINE bool SWGHasState(int64 StateBitmask, ESWGState State)
{
	const int64 Bit = SWGStateBit(State);
	return Bit != 0 && (StateBitmask & Bit) != 0;
}
