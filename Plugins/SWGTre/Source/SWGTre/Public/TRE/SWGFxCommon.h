#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/**
 * Pieces shared by the visual-effect formats (.prt, .swh, .ltn): PTIM timing,
 * WVFM curves, CLRR colour ramps and PTEX texture blocks. Layouts were pinned
 * down by dumping every retail .prt (1018 files, 2755 emitters) and then confirmed
 * against the retail client (SWGEmu.exe, see Docs/RetailParticleFormat.md for addresses).
 */

/**
 * PTIM 0001 (24 bytes): three min/max pairs, confirmed (loader 0x7c05b0, users 0x7c02e0/0x7c0320/0x7a2050).
 * Start delay applies before the first run, loop delay before every later run; a run's length is not here
 * (it is EMTR params RunTimeMin/Max). Absent PTIM defaults to zero delays and an infinite loop count (0x7c0200).
 */
struct FSWGEffectTiming
{
	bool bPresent = false;
	float StartDelayMin = 0.f;
	float StartDelayMax = 0.f;
	float LoopDelayMin = 0.f;
	float LoopDelayMax = 0.f;
	/** Extra runs after the first (0 = play once); negative = repeat forever. */
	int32 LoopCountMin = 0;
	int32 LoopCountMax = 0;
};

/**
 * One WVFM key: 4 floats, confirmed (0xabead0, sampler 0xabf530). Time is 0..1 and Value is clamped to +-10000.
 * RandomA is the downward spread and RandomB the upward spread (both >= 0): a sample is
 * Value + lerp(-RandomA, +RandomB, random01). Retail stores them as (up, down) in WVFM 0001 and (down, up)
 * in WVFM 0002; the reader normalises both to this order.
 */
struct FSWGWaveformPoint
{
	float Time = 0.f;
	float Value = 0.f;
	float RandomA = 0.f;
	float RandomB = 0.f;
};

/**
 * WVFM: a curve over normalised time (effect, emitter or particle life depending on where it sits).
 * 0001 = {int InterpolationMode, int SampleMode, int count, points}; 0002 inserts {float ClampMin, ClampMax}
 * before the count (confirmed: loaders 0xabfd00/0xabfe00). Retail meanings (sampler 0xabf530):
 *  - InterpolationMode 0 = linear between keys, 1 = cubic spline through the keys (Evaluate stays linear).
 *  - SampleMode 0 = one random per particle reused for every sample, 1 = a fresh random on every sample,
 *    anything else = no random spread at all.
 *  - the result is clamped to [ClampMin, ClampMax] (0002 only).
 */
struct SWGTRE_API FSWGWaveform
{
	int32 Version = 1;
	int32 InterpolationMode = 0;
	int32 SampleMode = 1;
	bool bHasClamp = false;
	float ClampMin = 0.f;
	float ClampMax = 0.f;
	TArray<FSWGWaveformPoint> Points;

	bool IsEmpty() const { return Points.IsEmpty(); }

	/** True when every key has the same value and no random spread. */
	bool IsConstant() const;

	/** Linear interpolation of the keys at Time (clamped to the first/last key); equal-time keys act as a step. */
	void Evaluate(float Time, float& OutValue, float& OutRandomA, float& OutRandomB) const;

	float EvaluateValue(float Time) const;

	/**
	 * Evaluate plus the random spread: Value + lerp(-A, +B, Random01), then the clamp. Random01 is the particle's
	 * persistent random; SampleMode 1 curves reroll it per call (deterministically from Random01 and Time), and
	 * SampleMode 2+ ignore it.
	 */
	float Sample(float Time, float Random01) const;
};

/**
 * CLRR 0001: {int, int, int count, count x (time, r, g, b)}. Confirmed (loader 0x7c1590). Mode1 is the second int
 * (read at 0x7c17c0): 0 = colour over the particle's life, 1 = one random point of the ramp picked at spawn.
 * Mode0 is stored but never read; both are 0 in nearly every file.
 */
struct FSWGParticleColorRampPoint
{
	float Time = 0.f;
	FLinearColor Color = FLinearColor::White;
};

struct SWGTRE_API FSWGParticleColorRamp
{
	int32 Mode0 = 0;
	int32 Mode1 = 0;
	TArray<FSWGParticleColorRampPoint> Points;

	FLinearColor Evaluate(float Time) const;
};

/**
 * PTEX 0000: shader path + 25 bytes, confirmed (loader 0x7b7370, frame picker 0x7b7100). Retail ignores FrameCount;
 * the strip length is FrameEnd - FrameStart + 1. fps exactly -1 picks FrameStart + floor(strip * lifeFraction);
 * fps > 0 loops FrameStart + floor(age * fps) over the strip; any other fps holds FrameStart. A frame maps to
 * column frame % GridColumns and row frame / GridColumns, each CellSize in UV.
 */
struct FSWGParticleTexture
{
	/** "shader/....sht" */
	FString ShaderPath;
	/** 1 = static, 4 = 2x2, 16 = 4x4. Not used by the retail frame picker. */
	int32 FrameCount = 1;
	/** First/last flipbook frame of the strip (0 and FrameCount-1 in most files). */
	int32 FrameStart = 0;
	int32 FrameEnd = 0;
	/** UV size of one flipbook cell = 1 / GridColumns. */
	float CellSize = 1.f;
	int32 GridColumns = 1;
	/** Playback rate in frames per second; exactly -1 = play the strip once across the particle's life. */
	float FramesPerSecond = -1.f;
	/** Stored at +0x2c and never read by the particle code; 0 only in the distant-ship sound emitters. */
	bool bFlag = true;
	/** The 25 bytes after the path, verbatim. */
	TArray<uint8> RawTail;
};

class SWGTRE_API FSWGFxCommonReader
{
public:
	static constexpr int32 PtexTailSize = 25;

	/** Reads a FORM WVFM. */
	static bool ReadWaveform(const FSWGIffReader& Reader, const FSWGIffChunk& WaveformForm, FSWGWaveform& OutWaveform);

	/** Reads a FORM CLRR. */
	static bool ReadColorRamp(const FSWGIffReader& Reader, const FSWGIffChunk& RampForm, FSWGParticleColorRamp& OutRamp);

	/** Reads a FORM PTIM. */
	static bool ReadTiming(const FSWGIffReader& Reader, const FSWGIffChunk& TimingForm, FSWGEffectTiming& OutTiming);

	/** Reads a FORM PTEX. */
	static bool ReadTexture(const FSWGIffReader& Reader, const FSWGIffChunk& TextureForm, FSWGParticleTexture& OutTexture);

	/** Backslashes to '/', for paths read from effect chunks. */
	static void NormalizePath(FString& Path) { Path.ReplaceInline(TEXT("\\"), TEXT("/")); }
};
