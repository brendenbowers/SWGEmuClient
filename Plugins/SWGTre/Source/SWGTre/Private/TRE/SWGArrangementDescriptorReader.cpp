#include "TRE/SWGArrangementDescriptorReader.h"

#include "TRE/SWGIFFChunkReader.h"

bool FSWGArrangementDescriptorReader::Read(const FSWGIffReader& Reader, TArray<FSWGArrangementGroup>& OutGroups)
{
	OutGroups.Reset();

	FSWGIffChunk ArgdForm, Form0000;
	if (!Reader.FindForm(SWG_IFF_TAG('A', 'R', 'G', 'D'), ArgdForm)
		|| !Reader.FindChildForm(ArgdForm, SWG_IFF_TAG('0', '0', '0', '0'), Form0000))
	{
		return false;
	}

	for (const FSWGIffChunk& Child : Reader.ReadChildren(Form0000))
	{
		if (Child.Tag != SWG_IFF_TAG('A', 'R', 'G', ' '))
		{
			continue;
		}

		FSWGIFFChunkReader ChunkReader(Child, Reader);
		FSWGArrangementGroup Group = ChunkReader.ReadTerminatedStrings(-1);
		OutGroups.Add(MoveTemp(Group));
	}

	return !OutGroups.IsEmpty();
}
