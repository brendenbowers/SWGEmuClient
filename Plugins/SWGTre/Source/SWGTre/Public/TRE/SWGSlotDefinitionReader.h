#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/**
 * One abstract/slot/slot_definition/slot_definitions.iff record. Field order
 * matches Struct[Slot] from the default templates in the IFF reader program
 * (source of truth):
 * {String[Name], Bool[Can Accept Any Item], Bool[Is Player Modifiable],
 *  Bool[Is Appearance Related], String[Hardpoint], Short[Combat Bone],
 *  Bool[Observe With Parent], Bool[Expose With Parent]}
 */
struct FSWGSlotDefinition
{
	FString Name;
	uint8 bCanAcceptAnyItem = 0;
	uint8 bIsPlayerModifiable = 0;
	uint8 bIsAppearanceRelated = 0;
	FString Hardpoint;
	int16 CombatBone = 0;
	uint8 bObserveWithParent = 0;
	uint8 bExposeWithParent = 0;
};

/** Reads abstract/slot/slot_definition/slot_definitions.iff. */
class SWGTRE_API FSWGSlotDefinitionReader
{
public:
	static bool Read(const FSWGIffReader& Reader, TArray<FSWGSlotDefinition>& OutDefinitions);
};
