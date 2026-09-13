#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/**
 * One FORM WEAR group from a mobile template's clientDataFile (.cdf): the
 * wearable meshes a "dressed_*" NPC is drawn with and the customization
 * (WCSI) that colors them. This is where most NPC clothing/armor comes from —
 * the server never sends TANO wearables for these templates; the outfit is
 * baked into the client data.
 */
struct FSWGClientDataWearable
{
	/** appearance/mesh/*.lmg paths — one skeletal wearable each. */
	TArray<FString> MeshPaths;

	/** Full customization variable name (e.g. "/private/index_color_1") -> value, applied to every mesh in this group. */
	TMap<FString, int32> Customization;
};

struct FSWGClientDataFile
{
	/** CSSI: the creature's own body customization ("/shared_owner/index_color_skin", "/shared_owner/blend_fat", ...). */
	TMap<FString, int32> Customization;

	TArray<FSWGClientDataWearable> Wearables;
};

/**
 * Reads clientdata/npc/*.cdf: FORM CLDF > FORM 0000 > CSSI chunks
 * ([name\0][int32 LE]) plus FORM WEAR groups of MESH ([path\0]) and WCSI
 * ([name\0][int32 LE]) chunks. Sound/effect chunks (CSND/CEFT/ASND) and the
 * rare PALV are skipped. Layout confirmed against every current .cdf in the
 * TRE set — WCSI always follows the group's MESH entries.
 */
class SWGTRE_API FSWGClientDataFileReader
{
public:
	static bool Read(const FSWGIffReader& Reader, FSWGClientDataFile& OutData);
};
