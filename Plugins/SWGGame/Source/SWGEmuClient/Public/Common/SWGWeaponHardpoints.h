#pragma once

#include "CoreMinimal.h"

/**
 * Hardpoint names weapon meshes carry (HPTS > HPNT; counted over every retail wp_*_l0.msh):
 * muzzle on guns, tr1s/tr1e (tr2s/tr2e on double-bladed weapons) the start and end of the trail edge on melee
 * weapons, whp1/whp2 the weapon-hit points, plus scope, stock, exhaust and emitter extras.
 */
namespace SWGWeaponHardpoint
{
	inline const FName Muzzle(TEXT("muzzle"));
	inline const FName TrailStart(TEXT("tr1s"));
	inline const FName TrailEnd(TEXT("tr1e"));
	inline const FName SecondTrailStart(TEXT("tr2s"));
	inline const FName SecondTrailEnd(TEXT("tr2e"));
}

/** CombatAction Trails bitmask (Core3 CombatManager): which limbs and weapon leave a trail. 0xFF is the default "all". */
namespace SWGCombatTrail
{
	constexpr uint8 LeftFoot = 0x01;
	constexpr uint8 RightFoot = 0x02;
	constexpr uint8 LeftHand = 0x04;
	constexpr uint8 RightHand = 0x08;
	constexpr uint8 Weapon = 0x10;
}
