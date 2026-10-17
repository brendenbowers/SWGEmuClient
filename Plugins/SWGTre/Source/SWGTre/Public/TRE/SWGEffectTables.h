#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGDataTableReader.h"

/**
 * Lookups into the datatables that connect weapons to visual effects.
 *
 *   datatables/weapon/weapon.iff    one row per (Weapon Effect Id, Weapon Effect Index); the "Projectile Appearance Template"
 *                                   column (3rd) names the bolt's .prt (e.g. appearance/pt_bolt_red.prt) next to the fire,
 *                                   hit, miss and ricochet .cef columns the audio path already reads.
 *   datatables/weapon/swoosh.iff    ObjectTemplateName (string) -> SwooshName (.swh path) + UseObjectHue (int).
 *   datatables/weapon/swoosh_hue.iff  HueMin, HueMax (float degrees), Red, Green, Blue (ints): the colour for a hue band.
 */
class SWGTRE_API FSWGEffectTables
{
public:
	/** The projectile .prt of a weapon.iff row, '/'-normalised; empty if the row is missing or has none. */
	static FString FindProjectileAppearance(const FSWGDataTableData& WeaponTable, const FString& EffectId, int32 EffectIndex);

	/**
	 * The swoosh a melee weapon template draws. Tries the template path as given, then with "shared_" added to / removed
	 * from the file name, since the client and server template names differ by that prefix.
	 */
	static bool FindSwoosh(const FSWGDataTableData& SwooshTable, const FString& ObjectTemplatePath, FString& OutSwooshPath, bool& bOutUseObjectHue);

	/** The colour of the swoosh_hue.iff row whose [HueMin, HueMax) contains HueDegrees. */
	static bool FindSwooshHueColor(const FSWGDataTableData& HueTable, float HueDegrees, FColor& OutColor);
};
