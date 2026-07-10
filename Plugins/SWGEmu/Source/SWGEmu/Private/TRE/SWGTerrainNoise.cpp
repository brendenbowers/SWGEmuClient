#include "TRE/SWGTerrainNoise.h"

int32 FSWGTerrainRandom::Next()
{
	auto StepLCG = [this]() -> int32
	{
		const int32 ValueTo1 = Seed;
		const int32 Value2 = (int32)((uint32)16807 * (uint32)ValueTo1);
		const int32 TempDiv = ValueTo1 / 0x1F31D;
		const int32 Value3 = (int32)((uint32)0x7FFFFFFF * (uint32)TempDiv);
		int32 NewSeed = Value2 - Value3;
		if (NewSeed < 0)
			NewSeed += 0x7FFFFFFF;
		return NewSeed;
	};

	if (Seed <= 0 || Unknown == 0)
	{
		const int32 V2 = -Seed;
		Seed = (V2 >= 1) ? V2 : 1;

		for (int32 i = 329; i >= 0; --i)
		{
			Seed = StepLCG();
			if (i < 322)
			{
				Table[i] = Seed;
			}
		}

		Unknown = Table[0];
	}

	Seed = StepLCG();

	const int32 Index = (int32)((double)Unknown / 6669205.0);
	Unknown = Table[Index];
	Table[Index] = Seed;

	return Unknown;
}

void FSWGPerlinNoise2D::Normalize2(float V[2])
{
	const double S = FMath::Sqrt((double)V[0] * V[0] + (double)V[1] * V[1]);
	V[0] = (float)((double)V[0] / S);
	V[1] = (float)((double)V[1] / S);
}

void FSWGPerlinNoise2D::Init(FSWGTerrainRandom& Rand)
{
	int32 i = 0;
	for (i = 0; i < B; ++i)
	{
		P[i] = i;

		// g1 is drawn and discarded here — only noise2 is needed (see header),
		// but this call must still happen to keep the RNG stream in sync with
		// what retail's PerlinNoise::init() consumes.
		(void)Rand.Next();

		for (int32 j = 0; j < 2; ++j)
		{
			G2[i][j] = (float)((Rand.Next() % (B + B)) - B) / (float)B;
		}
		Normalize2(G2[i]);
	}

	// Original: "while (--i)" — i is B (256) on loop exit above, runs for i = 255..1.
	while (--i)
	{
		const int32 j = Rand.Next() % B;
		const int32 k = P[i];
		P[i] = P[j];
		P[j] = k;
	}

	for (i = 0; i < B + 2; ++i)
	{
		P[B + i] = P[i];
		G2[B + i][0] = G2[i][0];
		G2[B + i][1] = G2[i][1];
	}
}

float FSWGPerlinNoise2D::Noise2(double Vec[2]) const
{
	auto Setup = [](double Coord, int32& B0, int32& B1, double& R0, double& R1)
	{
		const double T = Coord + (double)N;
		const int32 Trunc = (int32)T;
		const int32 Adjust = (T < 0.0 && T != (double)Trunc) ? 1 : 0;
		B0 = (Trunc - Adjust) & BM;
		B1 = (B0 + 1) & BM;
		R0 = T - (double)(Trunc - Adjust);
		R1 = R0 - 1.0;
	};

	int32 bx0, bx1, by0, by1;
	double rx0, rx1, ry0, ry1;
	Setup(Vec[0], bx0, bx1, rx0, rx1);
	Setup(Vec[1], by0, by1, ry0, ry1);

	const int32 i = P[bx0];
	const int32 j = P[bx1];

	const int32 b00 = P[i + by0];
	const int32 b10 = P[j + by0];
	const int32 b01 = P[i + by1];
	const int32 b11 = P[j + by1];

	const double sx = rx0 * rx0 * (3.0 - 2.0 * rx0);
	const double sy = ry0 * ry0 * (3.0 - 2.0 * ry0);

	const float* q;
	double u, v, a, b;

	q = G2[b00]; u = rx0 * q[0] + ry0 * q[1];
	q = G2[b10]; v = rx1 * q[0] + ry0 * q[1];
	a = u + sx * (v - u);

	q = G2[b01]; u = rx0 * q[0] + ry1 * q[1];
	q = G2[b11]; v = rx1 * q[0] + ry1 * q[1];
	b = u + sx * (v - u);

	return (float)(a + sy * (b - a));
}

void FSWGMapFractal::Initialize()
{
	FSWGTerrainRandom Rand;
	Rand.SetSeed(Seed);
	Noise.Init(Rand);

	float V3 = 0.0f, V2 = 1.0f;
	for (int32 i = 0; i < Octaves; ++i)
	{
		V3 += V2;
		V2 *= Amplitude;
	}
	Offset32 = (V3 != 0.0f) ? (1.0f / V3) : 0.0f;
}

double FSWGMapFractal::CalculateCombination1(float V39, float V41) const
{
	float V48 = 1.0f, V47 = 1.0f;
	const float V42 = V41 + ZOffset;
	const float V36 = V39 + XOffset;
	float V33 = 0.0f;
	double Coord[2];

	for (int32 i = 0; i < Octaves; ++i)
	{
		Coord[0] = V36 * V48;
		Coord[1] = V42 * V48;
		V33 = Noise.Noise2(Coord) * V47 + V33;
		V48 *= OctavesParam;
		V47 *= Amplitude;
	}

	return (V33 * Offset32 + 1.0) * 0.5;
}

double FSWGMapFractal::CalculateCombination2(float V39, float V41) const
{
	float V34 = 0.0f;
	const float V42 = V39 + XOffset;
	const float V43 = V41 + ZOffset;
	float V48 = 1.0f, V47 = 1.0f;
	double Coord[2];

	for (int32 i = 0; i < Octaves; ++i)
	{
		Coord[0] = V42 * V48;
		Coord[1] = V43 * V48;
		V34 = (1.0f - FMath::Abs(Noise.Noise2(Coord))) * V47 + V34;
		V48 *= OctavesParam;
		V47 *= Amplitude;
	}

	return V34 * Offset32;
}

double FSWGMapFractal::CalculateCombination3(float V39, float V41) const
{
	float V48 = 1.0f, V47 = 1.0f;
	float V34 = 0.0f;
	const float V44 = V41 + ZOffset;
	const float V22 = V39 + XOffset;
	double Coord[2];

	for (int32 i = 0; i < Octaves; ++i)
	{
		Coord[0] = V22 * V48;
		Coord[1] = V44 * V48;
		V34 = FMath::Abs(Noise.Noise2(Coord)) * V47 + V34;
		V48 *= OctavesParam;
		V47 *= Amplitude;
	}

	return V34 * Offset32;
}

double FSWGMapFractal::CalculateCombination4(float V39, float V41) const
{
	float V34 = 0.0f;
	const float V45 = V41 + ZOffset;
	const float V37 = V39 + XOffset;
	double Coord[2];
	float V48 = 1.0f, V47 = 1.0f;

	for (int32 i = 0; i < Octaves; ++i)
	{
		Coord[0] = V37 * V48;
		Coord[1] = V45 * V48;
		float V26 = Noise.Noise2(Coord);
		V26 = (V26 >= 0.0f) ? FMath::Min(V26, 1.0f) : 0.0f;
		V34 = (1.0f - V26) * V47 + V34;
		V48 *= OctavesParam;
		V47 *= Amplitude;
	}

	return V34 * Offset32;
}

double FSWGMapFractal::CalculateCombination5(float V39, float V41) const
{
	float V34 = 0.0f;
	const float V46 = V41 + ZOffset;
	const float V38 = V39 + XOffset;
	double Coord[2];
	float V48 = 1.0f, V47 = 1.0f;

	for (int32 i = 0; i < Octaves; ++i)
	{
		Coord[0] = V38 * V48;
		Coord[1] = V46 * V48;
		float V30 = Noise.Noise2(Coord);
		V30 = (V30 >= 0.0f) ? FMath::Min(V30, 1.0f) : 0.0f;
		V34 = V30 * V47 + V34;
		V48 *= OctavesParam;
		V47 *= Amplitude;
	}

	return V34 * Offset32;
}

float FSWGMapFractal::GetNoise(float X, float Y) const
{
	const float V39 = X * XFrequency;
	const float V41 = Y * YFrequency;

	double Result = 0.0;
	switch (Combination)
	{
		case 0:
		case 1: Result = CalculateCombination1(V39, V41); break;
		case 2: Result = CalculateCombination2(V39, V41); break;
		case 3: Result = CalculateCombination3(V39, V41); break;
		case 4: Result = CalculateCombination4(V39, V41); break;
		case 5: Result = CalculateCombination5(V39, V41); break;
		default: Result = 0.0; break;
	}

	static const double Log05 = FMath::Loge(0.5);

	// Result is meant to be a normalized [0,1] noise value, but the octave sum
	// above isn't strictly bounded — it occasionally dips just under 0 (Perlin
	// noise combined across octaves isn't a hard [-1,1] guarantee). Pow() with
	// a negative base and a non-integer exponent is undefined (NaN in
	// practice), and that NaN height baked into the landscape's uint16
	// heightmap is exactly what produced isolated needle-thin spike artifacts
	// scattered across otherwise normal terrain (confirmed via a live capture:
	// creature-scale geometry rendered correctly, but the terrain around it had
	// sparse extreme spikes, not a uniform scale/slope issue).
	Result = FMath::Clamp(Result, 0.0, 1.0);

	if (Bias != 0)
	{
		Result = FMath::Pow(Result, FMath::Loge((double)BiasValue) / Log05);
	}

	if (GainType != 0)
	{
		if (Result < 0.001) return 0.0f;
		if (Result > 0.999) return 1.0f;

		const double V40 = FMath::Loge(1.0 - (double)GainValue) / Log05;

		if (Result < 0.5)
		{
			return (float)(FMath::Pow(Result * 2.0, V40) * 0.5);
		}

		return (float)(1.0 - FMath::Pow((1.0 - Result) * 2.0, V40) * 0.5);
	}

	return (float)Result;
}
