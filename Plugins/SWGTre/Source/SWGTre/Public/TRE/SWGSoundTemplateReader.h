#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/** Retail sound categories (tail dword 10). Names beyond the obvious are inferred from which files carry each value. */
enum class ESWGSoundCategory : int32
{
	Ambient = 0,
	Explosion = 1,
	Item = 2,
	Movement = 3,
	UI = 4,
	Vehicle = 5,
	Creature = 6,
	Weapon = 7,
	BackgroundMusic = 8,
	PlayerMusic = 9,
	Machine = 10,
	Special = 12, // a single file (music_teammate_die.snd)
};

/**
 * Randomised parameter (SWGEmu.exe 0x41a5f0 fade-in, 0x41a6d0 fade-out, 0x41a7d0 volume, 0x41a940 pitch).
 * Volume/pitch: mode 0 = fixed (volume 1.0, pitch 0), 1 = uniform draw in [Min, Max] on the first loop iteration only,
 * 2 = a draw every iteration, 3 = drift: every ExtraA seconds a new target in [Min, Max], reached linearly over ExtraB seconds.
 * Fades: mode 0 = none, 1 = first (in) / last (out) iteration only, 2 = every iteration.
 */
struct FSWGSoundRandomRange
{
	int32 Mode = 0;
	float Min = 0.f;
	float Max = 0.f;
	float ExtraA = 0.f;
	float ExtraB = 0.f;
};

/**
 * One sound/*.snd. Layout (FORM SD2D > 0003, or FORM SD3D > FORM SD2D > 0003 + an empty 0001):
 *
 *   int32 sampleCount, sampleCount x [path\0], then 28 dwords (112 bytes):
 *
 *   t0  t1   float  start delay min/max (s)                         inferred: 0..0.3 on creature sounds, 2..5 on _os ambience
 *   t2  t3   float  fade-in min/max (s)                             inferred, mode in t14
 *   t4  t5   int32  loop count min/max, -1 = infinite               confirmed (_lp = -1, one-shots = 1)
 *   t6  t7   float  loop delay min/max (s)                          confirmed (_os random one-shots), mode in t13
 *   t8  t9   float  fade-out min/max (s)                            inferred, mode in t15
 *   t10      int32  category, see ESWGSoundCategory                 confirmed present, names inferred
 *   t11      int32  0/1 flag (retail default 1); no consumer found in SWGEmu.exe 0x41cfd0 load or the 0x40f590 start path   unknown
 *   t12      int32  always 0                                        unknown
 *   t13      int32  loop delay mode (0 none, 2 random)              inferred
 *   t14      int32  fade-in mode (0 none, 1 constant, 2 random)     inferred
 *   t15      int32  fade-out mode (0 none, 1 constant, 2 random)    inferred
 *   t16..t20       volume: mode, ExtraA(t17), min(t18), max(t19), ExtraB(t20)   min/max confirmed, scale 0..1
 *   t21..t25       pitch:  mode, ExtraA(t22), min(t23), max(t24), ExtraB(t25)   SEMITONES confirmed: factor = 2^(x/12) (0x419f00), rate clamped 5000..60000 Hz (0x40fe70)
 *   t26      int32  playback PRIORITY (0x41cfd0 forces 5 for Ambient, 3 for PlayerMusic; default 9); sounds are sorted by it and the lowest are dropped over the voice limit (0x40e030)
 *   t27      float  metres = Miles 3D MIN distance (full volume inside); MAX distance = 4 x t27 (0x412a00, default exponent 3); <= 0 on a 3D sound means 2 m; the client also stops sounds beyond max (0x4196c0). > 0 marks a 2D template spatial (0x41cfd0)
 *
 * Volume/pitch modes seen: 0 (min==max always), 1, 2 (random range), 3 (only ambient beds; drift with the extra floats).
 * Multi-PSND clienteffects are layered (door open = hydraulic + metal door), so play every sound, not one at random.
 */
struct FSWGSoundTemplate
{
	TArray<FString> Samples;
	bool b3D = false;
	/** Size of the SD3D 0001 chunk, -1 for 2D files (always 0 in retail data). */
	int32 Chunk3DSize = -1;

	FSWGSoundRandomRange StartDelay;
	FSWGSoundRandomRange FadeIn;
	FSWGSoundRandomRange FadeOut;
	FSWGSoundRandomRange LoopDelay;
	FSWGSoundRandomRange Volume;
	FSWGSoundRandomRange Pitch;

	int32 LoopCountMin = 1;
	int32 LoopCountMax = 1;

	int32 CategoryValue = 0;
	int32 UnknownFlag11 = 0;
	int32 Unknown12 = 0;
	int32 Unknown26 = 0;
	float DistanceMeters = 0.f;

	/** All 28 tail dwords verbatim, so nothing is lost if a meaning above turns out wrong. */
	TArray<uint32> RawTail;

	bool IsInfiniteLoop() const { return LoopCountMin < 0 || LoopCountMax < 0; }
	ESWGSoundCategory GetCategory() const { return static_cast<ESWGSoundCategory>(CategoryValue); }
};

class SWGTRE_API FSWGSoundTemplateReader
{
public:
	static constexpr int32 TailDwordCount = 28;

	static bool Read(const FSWGIffReader& Reader, FSWGSoundTemplate& OutTemplate);
};
