#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/** CPAP: particle attached to the effect. Fields beyond Scale appear only in newer FORM versions. */
struct FSWGClientEffectParticle
{
	FString Path;
	float Scale = 1.f;
	/** Version 0002+ (0 or 1); meaning unknown. */
	uint8 Flag = 0;
	/** Version 0003 only: four floats, 1.0 in nearly every file; probably an RGBA/modulation tint. */
	float Tint[4] = { 1.f, 1.f, 1.f, 1.f };
};

/**
 * CLGT: RGB bytes + 5 floats (SWGEmu.exe 0x5f05b0): Params[0] = lifetime in seconds (the light is removed then, no fade),
 * [1..3] = constant/linear/quadratic attenuation, [4] = range in metres.
 */
struct FSWGClientEffectLight
{
	uint8 Color[3] = {};
	float Params[5] = {};
};

/**
 * CAMS: 4 floats (SWGEmu.exe 0x5f0350, GameCamera alter 0x788740). Amplitude = total angle in RADIANS (random +-half per
 * frame, scaled by 1 - d^2/r^2), Duration = seconds with linear decay, Range = metres. Frequency (float 1) is stored but
 * the camera shake never reads it.
 */
struct FSWGClientEffectCameraShake
{
	float Amplitude = 0.f;
	float Frequency = 0.f;
	float Duration = 0.f;
	float Range = 0.f;
};

/** FFBK (version 0003): .ffe path, int32 (always 1 in samples seen), float range (m). */
struct FSWGClientEffectForceFeedback
{
	FString Path;
	int32 Flag = 0;
	float Range = 0.f;
};

/**
 * One clienteffect/*.cef: FORM CLEF > FORM 0001|0002|0003 holding any of PSND,
 * CPAP, CLGT, CAMS, FFBK. Several PSND are layered (door open = hydraulic +
 * metal door; grenade = hit + explosion), so playback should start them all.
 */
struct FSWGClientEffect
{
	/** 1, 2 or 3 (the inner FORM tag). */
	int32 Version = 0;

	/** PSND paths, backslashes normalised to '/'. */
	TArray<FString> SoundPaths;
	TArray<FSWGClientEffectParticle> Particles;
	TArray<FSWGClientEffectLight> Lights;
	TArray<FSWGClientEffectCameraShake> CameraShakes;
	TArray<FSWGClientEffectForceFeedback> ForceFeedbacks;
};

class SWGTRE_API FSWGClientEffectReader
{
public:
	/** False for an invalid/empty reader (one retail .cef is 0 bytes) or an unexpected layout. */
	static bool Read(const FSWGIffReader& Reader, FSWGClientEffect& OutEffect);
};
