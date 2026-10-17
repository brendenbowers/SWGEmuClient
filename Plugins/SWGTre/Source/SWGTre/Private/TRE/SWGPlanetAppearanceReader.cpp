#include "TRE/SWGPlanetAppearanceReader.h"
#include "TRE/SWGIFFChunkReader.h"

namespace
{
	bool PlanetFloat(FSWGIFFChunkReader& Data, float& Value)
	{
		return Data.ReadValueLE(Value) && FMath::IsFinite(Value);
	}
	bool PlanetSurface(FSWGIFFChunkReader& Data, FSWGPlanetSurface& Surface, bool bDetail)
	{
		if (!PlanetFloat(Data, Surface.SpinDegreesPerSecond) || !Data.ReadTerminiatedString(Surface.Shader)
			|| Surface.Shader.IsEmpty() || !PlanetFloat(Data, Surface.Radius) || Surface.Radius <= 0
			|| !PlanetFloat(Data, Surface.UVRepeat.X) || !PlanetFloat(Data, Surface.UVRepeat.Y)) return false;
		Surface.Shader.ReplaceInline(TEXT("\\"), TEXT("/"));
		return !bDetail || (PlanetFloat(Data, Surface.DetailUVRepeat.X) && PlanetFloat(Data, Surface.DetailUVRepeat.Y));
	}
}

bool FSWGPlanetAppearanceReader::Read(const FSWGIffReader& Reader, FSWGPlanetAppearanceData& Out)
{
	const TArray<FSWGIffChunk> Roots = Reader.ReadChunks();
	FSWGIffChunk Version;
	if (Roots.Num() != 1 || !Roots[0].IsForm() || Roots[0].FormType != SWG_IFF_TAG('P','L','N','T')
		|| !Reader.FindChildForm(Roots[0], SWG_IFF_TAG('0','0','0','0'), Version)) return false;
	FSWGPlanetAppearanceData Parsed;
	bool bInit = false, bSurface = false;
	for (const FSWGIffChunk& Chunk : Reader.ReadChildren(Version))
	{
		if (Chunk.IsForm()) return false;
		const FString Tag = Chunk.Tag.ToString();
		if (Tag != TEXT("INIT") && Tag != TEXT("SURF") && Tag != TEXT("CLOD") && Tag != TEXT("HALO")) continue;
		FSWGIFFChunkReader Data(Chunk, Reader);
		if (Tag == TEXT("INIT"))
		{
			if (bInit || !Data.ReadValueLE(Parsed.LatitudeSegments) || !Data.ReadValueLE(Parsed.LongitudeSegments)
				|| Parsed.LatitudeSegments < 2 || Parsed.LatitudeSegments > 512
				|| Parsed.LongitudeSegments < 2 || Parsed.LongitudeSegments > 512) return false;
			bInit = true;
		}
		else if (Tag == TEXT("SURF"))
		{
			if (bSurface || !PlanetSurface(Data, Parsed.Surface, true)) return false;
			bSurface = true;
		}
		else if (Tag == TEXT("CLOD"))
		{
			if (!Parsed.Clouds.Shader.IsEmpty() || !PlanetSurface(Data, Parsed.Clouds, false)) return false;
		}
		else if (Tag == TEXT("HALO"))
		{
			if (!Parsed.HaloShader.IsEmpty() || !Data.ReadTerminiatedString(Parsed.HaloShader)
				|| Parsed.HaloShader.IsEmpty() || !PlanetFloat(Data, Parsed.HaloRadiusRatio) || Parsed.HaloRadiusRatio <= 0) return false;
			Parsed.HaloShader.ReplaceInline(TEXT("\\"), TEXT("/"));
		}
		if (!Data.AtEnd()) return false;
	}
	if (!bInit || !bSurface) return false;
	Out = MoveTemp(Parsed);
	return true;
}
