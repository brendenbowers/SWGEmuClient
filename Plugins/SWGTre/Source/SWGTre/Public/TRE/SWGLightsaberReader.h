#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/** LGHT group of a .lsb (4 of 29 files; hilt glow light). Layout confirmed from data; reading as a point light is inferred. */
struct FSWGLightsaberLight
{
	/** COLR: RGBA bytes (32,32,32,255 in all four files). */
	FColor Color = FColor::White;
	/** RANG: two floats (1.0, 2.0). */
	float RangeMin = 0.f;
	float RangeMax = 0.f;
	/** TIME: two floats (0.02, 0.1) - flicker period bounds, seconds (inferred). */
	float TimeMin = 0.f;
	float TimeMax = 0.f;
	/** DAYN: 1 byte, 0 in all four files. Unknown. */
	uint8 DayNight = 0;
};

/** One BLAD group: the glowing blade quad. */
struct FSWGLightsaberBlade
{
	/** SHDR, usually shader/lightsaberblade.sht (an SWSH of 4 colour frames). */
	FString ShaderPath;
	/** LGTH metres, WDTH metres, OPEN/CLOS seconds to ignite/retract (confirmed by magnitude: 1.25 m, 0.1 m, 1.5 s). */
	float Length = 0.f;
	float Width = 0.f;
	float OpenSeconds = 0.f;
	float CloseSeconds = 0.f;
};

/**
 * appearance/wp_mle_*_lightsaber_blade_*.lsb (FORM LSAT > 0000), 29 files:
 *   BASE hilt .apt, ASND idle hum .snd, [FORM LGHT { COLR, RANG, TIME, DAYN }], FORM BLAD x1-2 { SHDR, LGTH, WDTH, OPEN, CLOS }.
 */
struct FSWGLightsaber
{
	FString HiltAppearancePath;
	FString HumSoundPath;
	bool bHasLight = false;
	FSWGLightsaberLight Light;
	TArray<FSWGLightsaberBlade> Blades;
};

class SWGTRE_API FSWGLightsaberReader
{
public:
	static bool Read(const FSWGIffReader& Reader, FSWGLightsaber& OutSaber);
};
