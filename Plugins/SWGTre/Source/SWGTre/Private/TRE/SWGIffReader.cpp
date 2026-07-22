#include "TRE/SWGIffReader.h"


FSWGIffReader::FSWGIffReader(TArray<uint8> InData)
	: Data(MoveTemp(InData))
{
}

int32 FSWGIffReader::ReadInt32BE(const uint8* Bytes)
{
	return (int32)((Bytes[0] << 24) | (Bytes[1] << 16) | (Bytes[2] << 8) | Bytes[3]);
}

FSWGIffTag FSWGIffReader::ReadTag(const uint8* Bytes)
{
	return FSWGIffTag((uint32(Bytes[0]) << 24) | (uint32(Bytes[1]) << 16) | (uint32(Bytes[2]) << 8) | uint32(Bytes[3]));
}

TArray<FSWGIffChunk> FSWGIffReader::ReadChunksInRange(int32 Pos, int32 End) const
{
	TArray<FSWGIffChunk> Result;

	while (Pos + 8 <= End)
	{
		const uint8* Bytes = Data.GetData();
		const FSWGIffTag Tag = ReadTag(Bytes + Pos);
		const int32 Size = ReadInt32BE(Bytes + Pos + 4);
		if (Size < 0 || Size > End - Pos - 8)
		{
			break;
		}

		FSWGIffChunk Chunk;
		Chunk.Tag = Tag;

		if (Tag == SWG_IFF_TAG('F', 'O', 'R', 'M'))
		{
			if (Size < 4)
				break;

			Chunk.FormType = ReadTag(Bytes + Pos + 8);
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

bool FSWGIffReader::FindFormRecursive(int32 Pos, int32 End, FSWGIffTag FormType, FSWGIffChunk& OutChunk) const
{
	const uint8* Bytes = Data.GetData();

	while (Pos + 8 <= End)
	{
		const FSWGIffTag Tag = ReadTag(Bytes + Pos);
		const int32 Size = ReadInt32BE(Bytes + Pos + 4);
		if (Size < 0 || Size > End - Pos - 8)
		{
			break;
		}

		if (Tag == SWG_IFF_TAG('F', 'O', 'R', 'M'))
		{
			if (Size < 4)
				break;

			const FSWGIffTag Type = ReadTag(Bytes + Pos + 8);
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

bool FSWGIffReader::FindForm(FSWGIffTag FormType, FSWGIffChunk& OutChunk) const
{
	return FindFormRecursive(0, Data.Num(), FormType, OutChunk);
}

bool FSWGIffReader::FindChildForm(const FSWGIffChunk& Parent, FSWGIffTag FormType, FSWGIffChunk& OutChunk) const
{
	for (const FSWGIffChunk& Child : ReadChildren(Parent))
	{
		if (Child.IsForm() && Child.FormType == FormType)
		{
			OutChunk = Child;
			return true;
		}
	}
	return false;
}

bool FSWGIffReader::FindChildChunk(const FSWGIffChunk& Parent, FSWGIffTag Tag, FSWGIffChunk& OutChunk) const
{
	for (const FSWGIffChunk& Child : ReadChildren(Parent))
	{
		if (!Child.IsForm() && Child.Tag == Tag)
		{
			OutChunk = Child;
			return true;
		}
	}
	return false;
}

TArray<FSWGIffChunk> FSWGIffReader::FindChildForms(const FSWGIffChunk& Parent) const
{
	TArray<FSWGIffChunk> Result;
	for (const FSWGIffChunk& Child : ReadChildren(Parent))
	{
		if (Child.IsForm())
			Result.Add(Child);
	}
	return Result;
}

TArray<FSWGIffChunk> FSWGIffReader::FindAllChildChunks(const FSWGIffChunk& Parent, FSWGIffTag Tag) const
{
	TArray<FSWGIffChunk> Result;
	for (const FSWGIffChunk& Child : ReadChildren(Parent))
	{
		if (!Child.IsForm() && Child.Tag == Tag)
		{
			Result.Add(Child);
		}
	}
	return Result;
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
		return FName(*TopLevel[0].FormType.ToString());
	}
	return NAME_None;
}
