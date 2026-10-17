#include "TRE/SWGCockpitReader.h"
#include "TRE/SWGIFFChunkReader.h"

namespace
{
	bool CockpitFloat(FSWGIFFChunkReader& Data, float& Value)
	{
		return Data.ReadValueLE(Value) && FMath::IsFinite(Value);
	}
	bool CockpitVector(FSWGIFFChunkReader& Data, FVector3f& Value)
	{
		return CockpitFloat(Data, Value.X) && CockpitFloat(Data, Value.Y)
			&& CockpitFloat(Data, Value.Z) && Data.AtEnd();
	}
}

bool FSWGCockpitReader::Read(const FSWGIffReader& Reader, FSWGCockpitData& Out)
{
	const TArray<FSWGIffChunk> Roots = Reader.ReadChunks();
	FSWGIffChunk Version;
	if (Roots.Num() != 1 || !Roots[0].IsForm() || Roots[0].FormType != SWG_IFF_TAG('C','P','I','T')
		|| !Reader.FindChildForm(Roots[0], SWG_IFF_TAG('0','0','0','0'), Version)) return false;
	FSWGCockpitData Parsed;
	TSet<FSWGIffTag> Seen;
	for (const FSWGIffChunk& Chunk : Reader.ReadChildren(Version))
	{
		if (Chunk.IsForm())
		{
			FSWGIffChunk Position;
			if (Chunk.FormType != SWG_IFF_TAG('I','S','P','B') || Parsed.NativePobPosition.IsSet()
				|| !Reader.FindChildChunk(Chunk, SWG_IFF_TAG('H','Y','P','R'), Position)) return false;
			FSWGIFFChunkReader Data(Position, Reader);
			FVector3f NativePosition;
			if (!CockpitVector(Data, NativePosition)) return false;
			Parsed.NativePobPosition = NativePosition;
			continue;
		}
		const FString Tag = Chunk.Tag.ToString();
		if (Tag != TEXT("FRAM") && Tag != TEXT("ZOOM") && Tag != TEXT("FRST")
			&& Tag != TEXT("3OFF") && Tag != TEXT("1OFF")) continue;
		if (Seen.Contains(Chunk.Tag)) return false;
		Seen.Add(Chunk.Tag);
		FSWGIFFChunkReader Data(Chunk, Reader);
		if (Tag == TEXT("FRAM"))
		{
			if (!Data.ReadTerminiatedString(Parsed.FrameAppearance)) return false;
			Parsed.FrameAppearance.ReplaceInline(TEXT("\\"), TEXT("/"));
		}
		else if (Tag == TEXT("ZOOM"))
		{
			if (Chunk.DataSize % 4 != 0 || Chunk.DataSize > 4096 * 4) return false;
			while (!Data.AtEnd())
			{
				float Distance;
				if (!CockpitFloat(Data, Distance)) return false;
				Parsed.ZoomDistances.Add(Distance);
			}
		}
		else if (Tag == TEXT("FRST"))
		{
			if (!CockpitFloat(Data, Parsed.FirstPersonThreshold)) return false;
		}
		else if (Tag == TEXT("3OFF"))
		{
			if (!CockpitVector(Data, Parsed.NativeThirdPersonOffset)) return false;
		}
		else if (!CockpitVector(Data, Parsed.NativeFirstPersonOffset)) return false;
		if (!Data.AtEnd()) return false;
	}
	if (Seen.Num() != 5) return false;
	Out = MoveTemp(Parsed);
	return true;
}
