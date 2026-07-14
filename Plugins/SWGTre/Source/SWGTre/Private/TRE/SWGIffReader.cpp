#include "TRE/SWGIffReader.h"

FSWGIffReader::FSWGIffReader(TArray<uint8> InData)
	: Data(MoveTemp(InData))
{
}

int32 FSWGIffReader::ReadInt32BE(const uint8* Bytes)
{
	return (int32)((Bytes[0] << 24) | (Bytes[1] << 16) | (Bytes[2] << 8) | Bytes[3]);
}

TArray<FSWGIffChunk> FSWGIffReader::ReadChunksInRange(int32 Pos, int32 End) const
{
	TArray<FSWGIffChunk> Result;

	while (Pos + 8 <= End)
	{
		const uint8* Bytes = Data.GetData();
		FString Tag = FString::ConstructFromPtrSize((const ANSICHAR*)(Bytes + Pos), 4);
		const int32 Size = ReadInt32BE(Bytes + Pos + 4);

		FSWGIffChunk Chunk;
		Chunk.Tag = Tag;

		if (Tag == TEXT("FORM"))
		{
			if (Pos + 12 > End)
				break;

			Chunk.FormType = FString::ConstructFromPtrSize((const ANSICHAR*)(Bytes + Pos + 8), 4);
			Chunk.DataOffset = Pos + 12;
			Chunk.DataSize = Size - 4; // Size covers FormType (4 bytes) + children
		}
		else
		{
			Chunk.DataOffset = Pos + 8;
			Chunk.DataSize = Size;
		}

		Result.Add(Chunk);
		Pos += 8 + Size;
	}

	return Result;
}

TArray<FSWGIffChunk> FSWGIffReader::ReadChunks() const
{
	return ReadChunksInRange(0, Data.Num());
}

TArray<FSWGIffChunk> FSWGIffReader::ReadChildren(const FSWGIffChunk& FormChunk) const
{
	if (!FormChunk.IsForm())
		return {};

	return ReadChunksInRange(FormChunk.DataOffset, FormChunk.DataOffset + FormChunk.DataSize);
}

bool FSWGIffReader::FindFormRecursive(int32 Pos, int32 End, const FString& FormType, FSWGIffChunk& OutChunk) const
{
	const uint8* Bytes = Data.GetData();

	while (Pos + 8 <= End)
	{
		FString Tag = FString::ConstructFromPtrSize((const ANSICHAR*)(Bytes + Pos), 4);
		const int32 Size = ReadInt32BE(Bytes + Pos + 4);

		if (Tag == TEXT("FORM"))
		{
			if (Pos + 12 > End)
				break;

			FString Type = FString::ConstructFromPtrSize((const ANSICHAR*)(Bytes + Pos + 8), 4);
			if (Type == FormType)
			{
				OutChunk.Tag = Tag;
				OutChunk.FormType = Type;
				OutChunk.DataOffset = Pos + 12;
				OutChunk.DataSize = Size - 4;
				return true;
			}

			if (FindFormRecursive(Pos + 12, Pos + 8 + Size, FormType, OutChunk))
				return true;
		}

		Pos += 8 + Size;
	}

	return false;
}

bool FSWGIffReader::FindForm(const FString& FormType, FSWGIffChunk& OutChunk) const
{
	return FindFormRecursive(0, Data.Num(), FormType, OutChunk);
}

const uint8* FSWGIffReader::GetChunkData(const FSWGIffChunk& Chunk) const
{
	return Data.GetData() + Chunk.DataOffset;
}

FName FSWGIffReader::GetRootFormType() const
{
	const TArray<FSWGIffChunk> TopLevel = ReadChunksInRange(0, Data.Num());
	if (TopLevel.Num() > 0 && TopLevel[0].IsForm())
	{
		return FName(*TopLevel[0].FormType);
	}
	return NAME_None;
}
