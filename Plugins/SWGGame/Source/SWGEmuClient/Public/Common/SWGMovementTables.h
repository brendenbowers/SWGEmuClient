#pragma once

#include "CoreMinimal.h"
#include "Common/SWGPostureTypes.h"

class USWGTreSubsystem;

/**
 * One row of datatables/movement/movement_human.iff — the per-posture movement
 * profile. Stationary/Slow/Fast are the locomotions this posture presents at
 * each speed bucket (Invalid where the posture can't move that fast, e.g.
 * Prone has no Fast); the four scales multiply the creature's CREO base4
 * movement fields.
 *
 * Retail ships exactly one of these tables and every creature template points
 * at it (movementDatatable = "datatables/movement/movement_human.iff" across
 * all of Core3's object scripts), so this is loaded once globally rather than
 * per-species.
 */
struct SWGEMUCLIENT_API FSWGPostureMovement
{
	ESWGLocomotion Stationary = ESWGLocomotion::Invalid;
	ESWGLocomotion Slow       = ESWGLocomotion::Invalid;
	ESWGLocomotion Fast       = ESWGLocomotion::Invalid;
	float MovementScale       = 1.0f;
	float AccelerationScale   = 1.0f;
	float TurnScale           = 1.0f;
	float CanSeeHeightMod     = 1.0f;
};

/**
 * The posture/locomotion/state reference tables, read once from the TREs at
 * client initialization and then read-only for the session — same "build the
 * global table during the loading state" shape as SWGDoorStyle/SWGResourceClass.
 *
 * Sources:
 *   datatables/include/{posture,locomotion,state}.iff  — name <-> value, used
 *       to verify the ESWG* enums in SWGPostureTypes.h still match the TREs
 *       this client is pointed at (a modded server can renumber them).
 *   datatables/movement/movement_human.iff             — FSWGPostureMovement rows
 *   datatables/movement/movementstates.iff             — per-state speed ceiling
 *   datatables/movement/state_rate_modifiers.iff       — per-state speed multiplier
 */
namespace SWGMovementTables
{
	/** Safe to call from a worker thread during loading; readers see the tables only once this returns. Returns false if any required table was missing or malformed, in which case the getters fall back to neutral values. */
	SWGEMUCLIENT_API bool Load(const USWGTreSubsystem& TreSubsystem);

	SWGEMUCLIENT_API bool IsLoaded();

	/** Null for a posture with no row in movement_human.iff (Invalid, and the datatable's own gaps). */
	SWGEMUCLIENT_API const FSWGPostureMovement* FindPosture(ESWGPosture Posture);

	/**
	 * The locomotion this posture presents at SpeedCategory, falling back down
	 * the buckets (Fast -> Slow -> Stationary) when the posture has no entry
	 * for the requested one — a prone creature moving at full tilt is still
	 * Crawling, not Running.
	 */
	SWGEMUCLIENT_API ESWGLocomotion ResolveLocomotion(ESWGPosture Posture, ESWGSpeedCategory SpeedCategory);

	/**
	 * Tightest speed ceiling imposed by the states currently set, per
	 * movementstates.iff (Aiming caps at Slow; Immobilized and Frozen at
	 * Stationary). Fast when no set state caps anything.
	 */
	SWGEMUCLIENT_API ESWGSpeedCategory GetMaxSpeedCategory(int64 StateBitmask);

	/** Product of every set state's movementRateModifier from state_rate_modifiers.iff (Swimming is 0.7 on retail). 1.0 when nothing applies. */
	SWGEMUCLIENT_API float GetStateRateModifier(int64 StateBitmask);

	/**
	 * Buckets an actual horizontal speed against the creature's own walk/run
	 * speeds. The midpoint split matches how the walk/run animation pair reads
	 * on screen better than a strict "above walk speed = fast" test would,
	 * since network-derived velocities hover around the walk speed constantly.
	 */
	SWGEMUCLIENT_API ESWGSpeedCategory ClassifySpeed(float Speed, float WalkSpeed, float RunSpeed);
}
