#include "TRE/SWGSpriteReader.h"

#include "TRE/SWGFxCommon.h"
#include "TRE/SWGIFFChunkReader.h"

FString FSWGSprite::GetShaderPath() const
{
	if (ShaderName.Contains(TEXT("/")) || ShaderName.EndsWith(TEXT(".sht"), ESearchCase::IgnoreCase))
	{
		return ShaderName;
	}
	return FString::Printf(TEXT("shader/%s.sht"), *ShaderName);
}

bool FSWGSpriteReader::Read(const FSWGIffReader& Reader, FSWGSprite& OutSprite)
{
	OutSprite = FSWGSprite();

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (!Reader.IsValid() || TopLevel.IsEmpty() || !TopLevel[0].IsForm())
	{
		return false;
	}

	const FSWGIffTag Type = TopLevel[0].FormType;
	if (Type != SWG_IFF_TAG('Q', 'U', 'A', 'D') && Type != SWG_IFF_TAG('S', 'P', 'R', 'T'))
	{
		return false;
	}
	OutSprite.bIsQuad = Type == SWG_IFF_TAG('Q', 'U', 'A', 'D');

	FSWGIffChunk DataChunk;
	if (!Reader.FindChildChunk(TopLevel[0], SWG_IFF_TAG('0', '0', '0', '0'), DataChunk))
	{
		return false;
	}

	FSWGIFFChunkReader Chunk(DataChunk, Reader);
	if (!Chunk.ReadValueLE(OutSprite.BoundsCenter.X) || !Chunk.ReadValueLE(OutSprite.BoundsCenter.Y)
		|| !Chunk.ReadValueLE(OutSprite.BoundsCenter.Z) || !Chunk.ReadValueLE(OutSprite.BoundsRadius)
		|| !Chunk.ReadValueLE(OutSprite.Tint.R) || !Chunk.ReadValueLE(OutSprite.Tint.G)
		|| !Chunk.ReadValueLE(OutSprite.Tint.B) || !Chunk.ReadValueLE(OutSprite.Tint.A)
		|| !Chunk.ReadTerminiatedString(OutSprite.ShaderName) || !Chunk.AtEnd())
	{
		return false;
	}
	FSWGFxCommonReader::NormalizePath(OutSprite.ShaderName);
	return !OutSprite.ShaderName.IsEmpty();
}
