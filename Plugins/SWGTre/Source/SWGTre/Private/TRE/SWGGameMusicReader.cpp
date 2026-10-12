#include "TRE/SWGGameMusicReader.h"

#include "TRE/SWGIFFChunkReader.h"

bool FSWGGameMusicReader::Read(const FSWGIffReader& Reader, FSWGGameMusicData& OutData)
{
	OutData = FSWGGameMusicData();

	FSWGIffChunk Root;
	if (!Reader.IsValid() || !Reader.FindForm(SWG_IFF_TAG('G', 'M', 'U', 'S'), Root))
	{
		return false;
	}

	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(Root);
	if (VersionForms.IsEmpty())
	{
		return false;
	}

	for (const FSWGIffChunk& Chunk : Reader.ReadChildren(VersionForms[0]))
	{
		if (Chunk.IsForm())
		{
			continue;
		}

		FString Path;
		FSWGIFFChunkReader ChunkReader(Chunk, Reader);
		if (!ChunkReader.ReadTerminiatedString(Path))
		{
			continue;
		}

		if (Chunk.Tag == SWG_IFF_TAG('E', 'N', 'T', 'R')) { OutData.CombatEnter.Add(Path); }
		else if (Chunk.Tag == SWG_IFF_TAG('E', 'X', 'I', 'T')) { OutData.CombatExit.Add(Path); }
		else if (Chunk.Tag == SWG_IFF_TAG('D', 'E', 'T', 'H')) { OutData.CombatDeath.Add(Path); }
		else if (Chunk.Tag == SWG_IFF_TAG('I', 'N', 'C', 'P')) { OutData.CombatIncapacitated.Add(Path); }
		else if (Chunk.Tag == SWG_IFF_TAG('S', 'E', 'N', 'T')) { OutData.SpaceEnter.Add(Path); }
		else if (Chunk.Tag == SWG_IFF_TAG('S', 'E', 'X', 'I')) { OutData.SpaceExit.Add(Path); }
		else if (Chunk.Tag == SWG_IFF_TAG('W', 'A', 'T', 'R')) { OutData.Water.Add(Path); }
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGGameMusicReader: unknown chunk %s"), *Chunk.Tag.ToString());
		}
	}

	return !OutData.CombatEnter.IsEmpty();
}
