#pragma once

#include "CoreMinimal.h"

/**
 * ESWGContainmentType enumerates the wire "containmentType" field carried on
 * FEquiptmentItem (WearablesDeltaVector entries) and FUpdateContainmentMessage.
 *
 * containmentType is not a small closed set — for ordinary clothing/armor/
 * weapon equip slots the value is SlottedArrangementBase + arrangementGroupIndex,
 * where the group index is looked up per-item from that item's ARGD
 * (arrangement descriptor) data, so no fixed name exists for most slotted
 * values. This enum names every value that IS fixed: the volume-contained
 * sentinel, the generic slotted-arrangement base, and Core3's PlayerArrangement
 * constants (src/templates/params/creature/PlayerArrangement.h) used for
 * mount/vehicle seating, which are genuine fixed wire values, not data-driven.
 * Everything else must still be handled as a raw int32 via the helpers below.
 *
 * CONFIRMED against Core3's ContainerComponent::canAddObject
 * (ContainerComponent.cpp, ~lines 64-155): VOLUME/INTANGIBLE/GENERIC
 * containers require containmentType != -1; SLOTTED/RIDABLE containers
 * require containmentType >= 4, with arrangementGroupIndex = containmentType - 4
 * (line ~131). Values 0-3 are never valid containmentType values on the wire
 * (0-3 are ContainerComponent::ContainerType policy values, a separate,
 * server-side-only concept — do not confuse the two).
 *
 * Rider == SlottedArrangementBase (both 4) is not a mistake: a mount's rider
 * seat is arrangement group index 0, i.e. SlottedArrangementBase + 0.
 */
enum class ESWGContainmentType : int32
{
	/** Item is volume-contained (e.g. sitting in an inventory/backpack), not occupying a named slot. */
	VolumeContained = -1,

	/** First valid slotted-arrangement value. ContainmentType - SlottedArrangementBase = zero-based ARG group index. */
	SlottedArrangementBase = 4,

	// ── PlayerArrangement values (Core3 PlayerArrangement.h) — fixed slotted-arrangement
	// values used for mount/ship seating, not data-driven like ordinary equip slots. ──
	Rider              = 4,
	ShipPilot          = 5,
	ShipOperations     = 6,
	ShipGunner0        = 7,
	ShipGunner1        = 8,
	ShipGunner2        = 9,
	ShipGunner3        = 10,
	ShipGunner4        = 11,
	ShipGunner5        = 12,
	ShipGunner6        = 13,
	ShipGunner7        = 14,
	ShipPilotPOB       = 15,
	ShipOperationsPOB  = 16,
	ShipGunner0POB     = 17,
	ShipGunner1POB     = 18,
	ShipGunner2POB     = 19,
	ShipGunner3POB     = 20,
	ShipGunner4POB     = 21,
	ShipGunner5POB     = 22,
	ShipGunner6POB     = 23,
	ShipGunner7POB     = 24,
};

/** True if this containmentType marks the item as volume-contained (not a slotted equip). */
FORCEINLINE bool SWGIsVolumeContained(int32 ContainmentType)
{
	return ContainmentType == static_cast<int32>(ESWGContainmentType::VolumeContained);
}

/** True if this containmentType selects a slotted arrangement (i.e. equipped in a named slot/slot group). */
FORCEINLINE bool SWGIsSlottedArrangement(int32 ContainmentType)
{
	return ContainmentType >= static_cast<int32>(ESWGContainmentType::SlottedArrangementBase);
}

/**
 * Resolves the zero-based ARG (arrangement) group index from a slotted containmentType value.
 * Caller must check SWGIsSlottedArrangement() first — this does not validate the range.
 */
FORCEINLINE int32 SWGGetArrangementGroupIndex(int32 ContainmentType)
{
	return ContainmentType - static_cast<int32>(ESWGContainmentType::SlottedArrangementBase);
}
