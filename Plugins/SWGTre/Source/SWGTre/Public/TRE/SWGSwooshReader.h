#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGFxCommon.h"
#include "TRE/SWGIffReader.h"

/**
 * One appearance/*.swh melee/missile trail (FORM SWSH > 0000|0001). Weapons reach it through datatables/weapon/swoosh.iff.
 * Decoded from all 48 retail files (45 x 0000, 6 x 0001):
 *
 *   FORM PTEX                 shader + flipbook block (same as a particle's)
 *   0000: float r,g,b,a, float lifetime, string trailEffect, then 27 (0000) or 39 (0001) fixed bytes.
 *
 * Confirmed: colour (0..1 floats, e.g. light_saber_red 1,.34,.34,1), lifetime (0.06 s for sword streaks up to
 * 4 s for missile trails), the optional child .prt (pt_*_trails). The fixed tail is typed below; its fields vary
 * file to file but their meaning is unknown.
 */
struct FSWGSwoosh
{
	/** 0 or 1. */
	int32 Version = 0;
	FSWGParticleTexture Texture;
	/** Straight RGBA multiplied into the shader (inferred: matches the colour in each file's name). */
	FLinearColor Color = FLinearColor::White;
	/** How long a trail segment lives, seconds (confirmed by scale: 0.06 swords, 4 s missiles). */
	float LifetimeSeconds = 0.f;
	/** Optional trailing particle effect, '/'-normalised (empty for most). */
	FString TrailEffectPath;

	/** Tail @2: 0.5/0.65/1/1.5/2/3/4/5 (U repeat or width scale - unknown). */
	float TailFloatA = 0.f;
	/** Tail @6: 0 mostly; -2.5..5 on missile/burning trails (looks like a UV scroll rate - unknown). */
	float TailFloatB = 0.f;
	/** Tail @10 / @14 int32: 1..6 and 0..6 enums - unknown. */
	int32 TailIntA = 0;
	int32 TailIntB = 0;
	/** Tail @18 byte, @19 int32 (1..3) and @23 int32 (always 1) - unknown. */
	bool bTailFlag = false;
	int32 TailIntC = 0;
	int32 TailIntD = 0;
	/** 0001 only: float 20/30, int 30/60, int 2/3 - unknown. */
	float ExtraFloat = 0.f;
	int32 ExtraIntA = 0;
	int32 ExtraIntB = 0;

	/** The bytes after the trail-effect string, verbatim (27 or 39). */
	TArray<uint8> RawTail;
};

class SWGTRE_API FSWGSwooshReader
{
public:
	static bool Read(const FSWGIffReader& Reader, FSWGSwoosh& OutSwoosh);
};
