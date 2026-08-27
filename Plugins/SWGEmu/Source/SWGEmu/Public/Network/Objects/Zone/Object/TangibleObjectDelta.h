#pragma once

#include "CoreMinimal.h"
#include "Network/SWGPacket.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "Network/Objects/Zone/Object/SWGDeltaListHelpers.h"

/**
 * Decoded updates from a TANO delta slot. A delta carries only the fields that
 * changed, so scalars are unset unless the message touched them and list
 * changes are empty unless it did.
 */
struct SWGEMU_API FTangibleObjectDelta
{
	// ── Base3 ──────────────────────────────────────────────────────
	TOptional<float>        Complexity;
	TOptional<FSWGStringId> ObjectName;
	TOptional<FString>      CustomName;
	TOptional<int32>        Volume;
	TOptional<TArray<uint8>> CustomizationBytes;
	TSWGListChanges<int32>  VisibleComponents;
	TOptional<int32>        OptionsBitmask;
	TOptional<int32>        UseCount;
	TOptional<int32>        ConditionDamage;
	TOptional<int32>        MaxCondition;
	TOptional<uint8>        ObjectVisible;

	// ── Base6 ──────────────────────────────────────────────────────
	TOptional<int32>        Unknown076;
	TSWGListChanges<uint64> DefenderList;
};

namespace SWGTangibleDeltaParser
{
	/** Reads one update's operand. False if Index isn't a tangible field, having read nothing. */
	SWGEMU_API bool ApplyUpdate3(FSWGPacket& Packet, FTangibleObjectDelta& Out, uint16 Index);
	SWGEMU_API bool ApplyUpdate6(FSWGPacket& Packet, FTangibleObjectDelta& Out, uint16 Index);

	SWGEMU_API void ParseDelta3(FSWGPacket& Packet, FTangibleObjectDelta& Out, uint16 UpdateCount);
	SWGEMU_API void ParseDelta6(FSWGPacket& Packet, FTangibleObjectDelta& Out, uint16 UpdateCount);
}
