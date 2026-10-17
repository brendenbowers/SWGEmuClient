#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGFxCommon.h"
#include "TRE/SWGIffReader.h"

/**
 * appearance/*.ltn force-lightning / nebula bolts (FORM LEFX; 38 files):
 *   0000|0001|0002: FORM PTEX, [FORM WVFM x2 (0001, 0002)], 0000 chunk.
 * The 0000 chunk is: float, start-effect .prt\0, end-effect .prt\0, then 61 (0000), 70 (0001) or 81 (0002) bytes.
 * Confirmed: the two particle paths and the shader. The leading float is 0.2 or 0.4 (a width, inferred).
 * Loader 0x7b3e90: the 0002 tail is a third string (usually empty, 1 byte) then 20 floats: 2 template floats, then two
 * identical 9-float layers (float, float, ARGB-without-alpha RGB x3, float, then 3 floats) - core and glow. Per-field meaning unverified;
 * kept raw until force lightning is implemented.
 */
struct FSWGLightningEffect
{
	/** 0, 1 or 2. */
	int32 Version = 0;
	FSWGParticleTexture Texture;
	/** Two curves in 0001/0002, none in 0000. Unknown meaning. */
	TArray<FSWGWaveform> Curves;
	float LeadFloat = 0.f;
	FString StartEffectPath;
	FString EndEffectPath;
	/** The bytes after the two strings, verbatim. */
	TArray<uint8> RawTail;
};

class SWGTRE_API FSWGLightningEffectReader
{
public:
	static bool Read(const FSWGIffReader& Reader, FSWGLightningEffect& OutEffect);
};
