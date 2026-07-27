#include "TRE/SWGCustomizationIdManager.h"
#include "TRE/SWGIffTags.h"
#include "TRE/SWGIFFChunkReader.h"

bool FSWGCustomizationIdManager::Read(const FSWGIffReader& Reader, FSWGCustomizationIdManager& OutResult)
{
	if (!Reader.IsValid())
	{
		return false;
	}

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0 || !TopLevel[0].IsForm() || TopLevel[0].FormType != SWG_IFF_TAG('C','I','D','M'))
	{
		return false;
	}

	FSWGIffChunk Form0001, DataChunk;
	if (!Reader.FindChildForm(TopLevel[0], SWG_IFF_TAG('0','0','0','1'), Form0001)) return false;
	if (!Reader.FindChildChunk(Form0001, SWG_IFF_TAG('D','A','T','A'), DataChunk)) return false;

	FSWGIFFChunkReader ChunkReader(DataChunk, Reader);
	while (ChunkReader.CanRead<int16>())
	{
		const int16 Id = ChunkReader.ReadValueLE<int16>();
		FString Var;
		if (!ChunkReader.ReadTerminiatedString(Var))
		{
			break;
		}

		OutResult.IdToName.Add((uint8)Id, Var);
		OutResult.NameToId.Add(Var, (uint8)Id);
	}

	return OutResult.IdToName.Num() > 0;
}
