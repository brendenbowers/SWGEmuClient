#include "TRE/SWGSpaceTerrainReader.h"
#include "TRE/SWGIFFChunkReader.h"

namespace
{
	bool Float(FSWGIFFChunkReader& Reader, float& Value)
	{
		return Reader.ReadValueLE(Value) && FMath::IsFinite(Value);
	}
	bool Color(FSWGIFFChunkReader& Reader, FLinearColor& Value)
	{
		return Float(Reader, Value.A) && Float(Reader, Value.R)
			&& Float(Reader, Value.G) && Float(Reader, Value.B);
	}
	bool SpaceVector(FSWGIFFChunkReader& Reader, FVector& Value)
	{
		float X, Y, Z;
		if (!Float(Reader, X) || !Float(Reader, Y) || !Float(Reader, Z)) return false;
		Value = FVector(X, Y, Z);
		return true;
	}
}

bool FSWGSpaceTerrainReader::Read(const FSWGIffReader& Reader, FSWGSpaceTerrainData& Out)
{
	const TArray<FSWGIffChunk> Roots = Reader.ReadChunks();
	FSWGIffChunk Version;
	if (Roots.Num() != 1 || !Roots[0].IsForm()
		|| Roots[0].FormType != SWG_IFF_TAG('S','T','A','T')
		|| !Reader.FindChildForm(Roots[0], SWG_IFF_TAG('0','0','0','0'), Version)) return false;
	FSWGSpaceTerrainData Parsed;
	bool bClear = false, bAmbient = false;
	for (const FSWGIffChunk& Form : Reader.ReadChildren(Version))
	{
		if (!Form.IsForm()) return false;
		const FString Tag = Form.FormType.ToString();
		// shortcut: space fog is retained by retail but its renderer is pending.
		if (Tag != TEXT("CLEA") && Tag != TEXT("AMBI") && Tag != TEXT("PARA")
			&& Tag != TEXT("SKYB") && Tag != TEXT("ENVI") && Tag != TEXT("STAR") && Tag != TEXT("DUST")
			&& Tag != TEXT("PLAN") && Tag != TEXT("CELE")) continue;
		FSWGIffChunk Chunk;
		if (!Reader.FindChildChunk(Form, SWG_IFF_TAG('0','0','0','0'), Chunk)) return false;
		FSWGIFFChunkReader Data(Chunk, Reader);
		if (Tag == TEXT("CLEA"))
		{
			if (!Float(Data, Parsed.ClearColor.R) || !Float(Data, Parsed.ClearColor.G) || !Float(Data, Parsed.ClearColor.B)) return false;
			bClear = true;
		}
		else if (Tag == TEXT("AMBI"))
		{
			if (!Color(Data, Parsed.Ambient)) return false;
			bAmbient = true;
		}
		else if (Tag == TEXT("PARA"))
		{
			FSWGSpaceLight Light;
			float X = 0, Y = 0, Z = 0;
			if (!Data.ReadValueLE(Light.Flags) || !Color(Data, Light.Diffuse) || !Color(Data, Light.Specular)
				|| !Float(Data, X) || !Float(Data, Y) || !Float(Data, Z)) return false;
			Light.NativeVector = FVector(X, Y, Z);
			Parsed.Lights.Add(Light);
		}
		else if (Tag == TEXT("SKYB"))
		{
			if (!Data.ReadValueLE(Parsed.SkyboxFlags) || !Data.ReadTerminiatedString(Parsed.Skybox)) return false;
		}
		else if (Tag == TEXT("ENVI"))
		{
			if (!Data.ReadTerminiatedString(Parsed.EnvironmentTexture)) return false;
		}
		else if (Tag == TEXT("STAR"))
		{
			if (!Data.ReadTerminiatedString(Parsed.StarColorRamp) || !Data.ReadValueLE(Parsed.StarCount)
				|| Parsed.StarCount < 0 || Parsed.StarCount > 1000000) return false;
		}
		else if (Tag == TEXT("DUST"))
		{
			if (!Data.ReadValueLE(Parsed.DustCount) || Parsed.DustCount < 0 || Parsed.DustCount > 1000000
				|| !Float(Data, Parsed.DustRadius) || Parsed.DustRadius < 0) return false;
		}
		else if (Tag == TEXT("PLAN"))
		{
			FSWGSpacePlanet Planet;
			if (Parsed.Planets.Num() >= 4096 || !Data.ReadTerminiatedString(Planet.Appearance) || Planet.Appearance.IsEmpty()
				|| !SpaceVector(Data, Planet.NativePosition) || !SpaceVector(Data, Planet.NativeAnglesDegrees)
				|| !Float(Data, Planet.HaloAngleDegrees) || !Float(Data, Planet.HaloScale)) return false;
			Planet.Appearance.ReplaceInline(TEXT("\\"), TEXT("/"));
			Parsed.Planets.Add(MoveTemp(Planet));
		}
		else if (Tag == TEXT("CELE"))
		{
			FSWGSpaceCelestial Celestial;
			if (Parsed.Celestials.Num() >= 4096 || !Data.ReadTerminiatedString(Celestial.Shader) || Celestial.Shader.IsEmpty()
				|| !Float(Data, Celestial.Size) || Celestial.Size < 0
				|| !Data.ReadTerminiatedString(Celestial.GlowShader) || !Float(Data, Celestial.GlowSize) || Celestial.GlowSize < 0
				|| !SpaceVector(Data, Celestial.NativeDirection) || Celestial.NativeDirection.IsNearlyZero()) return false;
			Celestial.Shader.ReplaceInline(TEXT("\\"), TEXT("/"));
			Celestial.GlowShader.ReplaceInline(TEXT("\\"), TEXT("/"));
			Parsed.Celestials.Add(MoveTemp(Celestial));
		}
		if (!Data.AtEnd()) return false;
	}
	if (!bClear || !bAmbient) return false;
	Out = MoveTemp(Parsed);
	return true;
}
