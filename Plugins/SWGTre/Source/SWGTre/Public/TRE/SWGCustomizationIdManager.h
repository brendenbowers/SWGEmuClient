#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/**
 * Reads "customization/customization_id_manager.iff" — the global mapping
 * between the numeric TypeId a TANO/CREO's wire "customization" field uses
 * (see FSWGCustomizationVariables) and its real variable name (e.g.
 * "/private/index_color_1"). Mirrors Core3's CustomizationIdManager::readObject
 * (MMOCoreORB/src/templates/customization/CustomizationIdManager.cpp):
 * wire layout is FORM CIDM > FORM 0001 > CHUNK DATA, the DATA chunk holding
 * repeated {int16 Id, null-terminated string VariableName} records read
 * until the chunk is exhausted.
 */
struct SWGTRE_API FSWGCustomizationIdManager
{
	TMap<uint8, FString> IdToName;
	TMap<FString, uint8> NameToId;

	static bool Read(const FSWGIffReader& Reader, FSWGCustomizationIdManager& OutResult);
};
