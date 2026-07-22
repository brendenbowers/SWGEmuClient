#include "TRE/SWGIFFChunkReader.h"


bool FSWGIFFChunkReader::ReadTerminiatedString(FString& Value)
{
	if (!Data || Position < 0 || Position >= Chunk.DataSize)
	{
		return false;
	}

	const int32 Start = Position;
	while (Position < Chunk.DataSize && Data[Position] != 0)
	{
		Position++;
	}
	if (Position == Chunk.DataSize)
	{
		return false;
	}

	Value = FString::ConstructFromPtrSize((const ANSICHAR*)(Data + Start), Position - Start);
	Position++; // null terminator

	return true;
}

bool FSWGIFFChunkReader::ReadTerminatedStrings(TArray<FString>& Values, int32 Count)
{
	if (!Data || Position < 0 || Position >= Chunk.DataSize)
	{
		return false;
	}

	
	// if -1 was given as the count, we will just read the whole chunk as strings.
	for (int32 i = 0; Position < Chunk.DataSize && ((Count >= 0 && i < Count) || (Count < 0)); ++i)
	{
		int32 End = Position;
		while (End < Chunk.DataSize && Data[End] != 0)
		{
			++End;
		}

		if (End >= Chunk.DataSize)
		{
			return false;
		}

		const int32 Length = End - Position;
		if (Length == 0)
		{
			Values.Add(FString());
		}
		else
		{
			Values.Add(FString::ConstructFromPtrSize((const ANSICHAR*)(Data + Position), Length));
		}

		Position = End + 1; // skip the null terminator
	}
	return true;
}

bool FSWGIFFChunkReader::SkipString()
{
	if (!Data || Position < 0 || Position >= Chunk.DataSize)
	{
		return false;
	}

	const int32 Start = Position;
	while (Position < Chunk.DataSize && Data[Position] != 0)
	{
		Position++;
	}
	if (Position == Chunk.DataSize)
	{
		return false;
	}

	Position++;
	return true;
}
