#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

struct FSWGSlotDefinition
{
	FString Name;
	// Flag meanings are inferred from correlations in the shipped slot definitions.
	uint8 UnrestrictedFlag = 0;
	uint8 PlayerModifiableFlag = 0;
	uint8 AppearanceRelatedFlag = 0;
	FString HardpointName;
	uint16 AnatomicalRegionMask = 0;
	uint8 SpecialContainmentFlag = 0;
	uint8 RiderFlag = 0;
};

/** Reads abstract/slot/slot_definition/slot_definitions.iff. */
class SWGTRE_API FSWGSlotDefinitionReader
{
public:
	static bool Read(const FSWGIffReader& Reader, TArray<FSWGSlotDefinition>& OutDefinitions);
};
