#include "TRE/SWGSwooshReader.h"

#include "TRE/SWGIFFChunkReader.h"

bool FSWGSwooshReader::Read(const FSWGIffReader& Reader, FSWGSwoosh& OutSwoosh)
{
	OutSwoosh = FSWGSwoosh();

	FSWGIffChunk SwshForm;
	if (!Reader.IsValid() || !Reader.FindForm(SWG_IFF_TAG('S', 'W', 'S', 'H'), SwshForm))
	{
		return false;
	}
	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(SwshForm);
	if (VersionForms.IsEmpty())
	{
		return false;
	}
	OutSwoosh.Version = FCString::Atoi(*VersionForms[0].FormType.ToString());

	FSWGIffChunk TextureForm, DataChunk;
	if (OutSwoosh.Version > 1
		|| !Reader.FindChildForm(VersionForms[0], SWG_IFF_TAG('P', 'T', 'E', 'X'), TextureForm)
		|| !FSWGFxCommonReader::ReadTexture(Reader, TextureForm, OutSwoosh.Texture)
		|| !Reader.FindChildChunk(VersionForms[0], SWG_IFF_TAG('0', '0', '0', '0'), DataChunk))
	{
		return false;
	}

	FSWGIFFChunkReader Chunk(DataChunk, Reader);
	const int32 TailSize = OutSwoosh.Version == 0 ? 27 : 39;
	uint8 Flag = 0;
	if (!Chunk.ReadValueLE(OutSwoosh.Color.R) || !Chunk.ReadValueLE(OutSwoosh.Color.G) || !Chunk.ReadValueLE(OutSwoosh.Color.B)
		|| !Chunk.ReadValueLE(OutSwoosh.Color.A) || !Chunk.ReadValueLE(OutSwoosh.LifetimeSeconds)
		|| !Chunk.ReadTerminiatedString(OutSwoosh.TrailEffectPath) || Chunk.GetRemaining() != TailSize)
	{
		return false;
	}
	FSWGFxCommonReader::NormalizePath(OutSwoosh.TrailEffectPath);

	const uint8* TailBytes = Reader.GetChunkData(DataChunk) + Chunk.GetPosition();
	OutSwoosh.RawTail.Append(TailBytes, TailSize);

	uint8 SkippedByte = 0;
	bool bOk = Chunk.ReadValueLE(SkippedByte) && Chunk.ReadValueLE(SkippedByte) && Chunk.ReadValueLE(OutSwoosh.TailFloatA)
		&& Chunk.ReadValueLE(OutSwoosh.TailFloatB) && Chunk.ReadValueLE(OutSwoosh.TailIntA) && Chunk.ReadValueLE(OutSwoosh.TailIntB)
		&& Chunk.ReadValueLE(Flag) && Chunk.ReadValueLE(OutSwoosh.TailIntC) && Chunk.ReadValueLE(OutSwoosh.TailIntD);
	OutSwoosh.bTailFlag = Flag != 0;
	if (bOk && OutSwoosh.Version == 1)
	{
		bOk = Chunk.ReadValueLE(OutSwoosh.ExtraFloat) && Chunk.ReadValueLE(OutSwoosh.ExtraIntA) && Chunk.ReadValueLE(OutSwoosh.ExtraIntB);
	}
	return bOk && Chunk.AtEnd();
}
