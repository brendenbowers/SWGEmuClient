#include "TRE/SWGClientDataFileReader.h"

#include "TRE/SWGIFFChunkReader.h"

namespace
{
	// CSSI and WCSI share one layout: [name\0][int32 LE].
	void ReadNamedInt(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk, TMap<FString, int32>& OutValues)
	{
		FSWGIFFChunkReader ChunkReader(Chunk, Reader);
		ChunkReader.ReadMapToEnd<FString, int32>(OutValues,
			[&ChunkReader](FString& Name) { return ChunkReader.ReadTerminiatedString(Name) && !Name.IsEmpty(); },
			[&ChunkReader](int32& Value) { return ChunkReader.ReadValueLE(Value); });
	}
}

bool FSWGClientDataFileReader::Read(const FSWGIffReader& Reader, FSWGClientDataFile& OutData)
{
	OutData = FSWGClientDataFile();

	FSWGIffChunk CldfForm, Form0000;
	if (!Reader.IsValid()
		|| !Reader.FindForm(SWG_IFF_TAG('C', 'L', 'D', 'F'), CldfForm)
		|| !Reader.FindChildForm(CldfForm, SWG_IFF_TAG('0', '0', '0', '0'), Form0000))
	{
		return false;
	}

	for (const FSWGIffChunk& Child : Reader.ReadChildren(Form0000))
	{
		if (Child.Tag == SWG_IFF_TAG('C', 'S', 'S', 'I'))
		{
			ReadNamedInt(Reader, Child, OutData.Customization);
			continue;
		}

		if (!Child.IsForm() || Child.FormType != SWG_IFF_TAG('W', 'E', 'A', 'R'))
		{
			continue;
		}

		FSWGClientDataWearable Wearable;
		for (const FSWGIffChunk& WearChild : Reader.ReadChildren(Child))
		{
			if (WearChild.Tag == SWG_IFF_TAG('M', 'E', 'S', 'H'))
			{
				FSWGIFFChunkReader ChunkReader(WearChild, Reader);
				FString MeshPath;
				if (ChunkReader.ReadTerminiatedString(MeshPath) && !MeshPath.IsEmpty())
				{
					Wearable.MeshPaths.Add(MoveTemp(MeshPath));
				}
			}
			else if (WearChild.Tag == SWG_IFF_TAG('W', 'C', 'S', 'I'))
			{
				ReadNamedInt(Reader, WearChild, Wearable.Customization);
			}
		}

		if (!Wearable.MeshPaths.IsEmpty())
		{
			OutData.Wearables.Add(MoveTemp(Wearable));
		}
	}

	return true;
}
