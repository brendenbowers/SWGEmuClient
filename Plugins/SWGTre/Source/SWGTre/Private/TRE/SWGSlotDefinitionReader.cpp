#include "TRE/SWGSlotDefinitionReader.h"

#include "TRE/SWGIFFChunkReader.h"

bool FSWGSlotDefinitionReader::Read(const FSWGIffReader& Reader, TArray<FSWGSlotDefinition>& OutDefinitions)
{
	OutDefinitions.Reset();

	FSWGIffChunk VersionForm;
	FSWGIffChunk DataChunk;
	if (!Reader.FindForm(SWG_IFF_TAG('0', '0', '0', '6'), VersionForm)
		|| !Reader.FindChildChunk(VersionForm, SWG_IFF_TAG('D', 'A', 'T', 'A'), DataChunk))
	{
		return false;
	}

	FSWGIFFChunkReader Data(DataChunk, Reader);
	while (!Data.AtEnd())
	{
		FSWGSlotDefinition Definition;
		if (!Data.ReadTerminiatedString(Definition.Name)
			|| !Data.ReadValueLE(Definition.bCanAcceptAnyItem)
			|| !Data.ReadValueLE(Definition.bIsPlayerModifiable)
			|| !Data.ReadValueLE(Definition.bIsAppearanceRelated)
			|| !Data.ReadTerminiatedString(Definition.Hardpoint)
			|| !Data.ReadValueLE(Definition.CombatBone)
			|| !Data.ReadValueLE(Definition.bObserveWithParent)
			|| !Data.ReadValueLE(Definition.bExposeWithParent))
		{
			OutDefinitions.Reset();
			return false;
		}

		if (!Definition.Name.IsEmpty())
		{
			OutDefinitions.Add(MoveTemp(Definition));
		}
	}

	return !OutDefinitions.IsEmpty();
}
