#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/**
 * One ARG group from an item's arrangementDescriptorFilename (.iff) —
 * abstract/slot/arrangement/wearables/*.iff. Each group is one ordered set of
 * slot names the item occupies when equipped with the matching
 * containmentType: SWGGetArrangementGroupIndex(ContainmentType)
 * (SWGContainmentType.h) indexes the array this reader returns. Most items
 * have exactly one group; a few (mainly multi-slot outfits like a jumpsuit)
 * list more than one slot name per group.
 */
using FSWGArrangementGroup = TArray<FString>;

/**
 * Reads an item's ARGD (arrangement descriptor) file: FORM ARGD > FORM 0000 >
 * one or more "ARG " chunks, each holding that group's slot names as
 * consecutive null-terminated strings — same packing
 * USWGMeshGeneratorSubsystem already unpacks for a .sat's MSGN chunk (see
 * ResolveMeshPathForTemplate).
 */
class SWGTRE_API FSWGArrangementDescriptorReader
{
public:
	static bool Read(const FSWGIffReader& Reader, TArray<FSWGArrangementGroup>& OutGroups);
};
