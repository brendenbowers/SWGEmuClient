#pragma once

#include "CoreMinimal.h"

/**
 * Exact port of Core3's trn::ptat::Random (terrain/Random.h) — a Park-Miller
 * minimal-standard LCG (a=16807, m=2^31-1) with a 322-entry Bays-Durham shuffle
 * table. Must be replicated bit-for-bit (including discarded calls — see
 * FSWGPerlinNoise2D::Init) since it seeds the Perlin gradient table that
 * determines the exact shape of retail planet terrain.
 */
class SWGEMU_API FSWGTerrainRandom
{
public:
	void SetSeed(int32 InSeed) { Seed = -InSeed; }
	int32 Next();

private:
	int32 Seed = 0;
	int32 Unknown = 0;
	int32 Table[322] = {};
};

/**
 * Exact port of Core3's PerlinNoise (terrain/PerlinNoise.h), 2D case only —
 * AffectorHeightFractal only ever calls getNoise(x,y,0,0), which routes to
 * noise2 in every MapFractal combination mode, so the 1D path (noise1/g1) is
 * intentionally not ported. g1 values ARE still drawn from the RNG during Init
 * (and discarded) purely to keep the random stream in sync with the original.
 */
class SWGEMU_API FSWGPerlinNoise2D
{
public:
	void Init(FSWGTerrainRandom& Rand);
	float Noise2(double Vec[2]) const;

private:
	static constexpr int32 B = 0x100;
	static constexpr int32 BM = 0xff;
	static constexpr int32 N = 0x1000;

	int32 P[B + B + 2] = {};
	float G2[B + B + 2][2] = {};

	static void Normalize2(float V[2]);
};

/**
 * Exact port of Core3's MapFractal (terrain/MapFractal.h/.cpp) — evaluates one
 * of five octave-summed 2D Perlin noise "combination" modes plus optional
 * bias/gain reshaping. Field layout and formulas confirmed field-for-field
 * against MapFractal::parseFromIffStream/getNoise — see world-object-plan.html
 * "Terrain rendering".
 */
struct SWGEMU_API FSWGMapFractal
{
	int32 Seed = 0;
	int32 Bias = 0;
	float BiasValue = 0.5f;
	int32 GainType = 0;
	float GainValue = 0.7f;
	int32 Octaves = 2;
	float OctavesParam = 0.0f;
	float Amplitude = 0.5f;
	float XFrequency = 0.01f;
	float YFrequency = 0.01f;
	float XOffset = 0.0f;
	float ZOffset = 0.0f;
	int32 Combination = 0;

	/** Call after setting Seed/Octaves/Amplitude from the parsed fields — builds the noise table and derived Offset32. */
	void Initialize();

	float GetNoise(float X, float Y) const;

private:
	FSWGPerlinNoise2D Noise;
	float Offset32 = 1.0f;

	double CalculateCombination1(float V39, float V41) const;
	double CalculateCombination2(float V39, float V41) const;
	double CalculateCombination3(float V39, float V41) const;
	double CalculateCombination4(float V39, float V41) const;
	double CalculateCombination5(float V39, float V41) const;
};

/** FractalId -> FSWGMapFractal, resolved from a .trn's MGRP > MFAM(*) > MFRC entries. */
struct SWGEMU_API FSWGMapGroup
{
	TMap<int32, FSWGMapFractal> Fractals;

	const FSWGMapFractal* FindFractal(int32 FractalId) const { return Fractals.Find(FractalId); }
};
