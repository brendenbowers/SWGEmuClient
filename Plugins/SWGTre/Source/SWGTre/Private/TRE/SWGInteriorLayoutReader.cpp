#include "TRE/SWGInteriorLayoutReader.h"
#include "TRE/SWGIFFChunkReader.h"
#include "Common/SWGWorldScale.h"

bool FSWGInteriorLayoutReader::ReadInteriorLayout(const FSWGIffReader& Reader, FSWGInteriorLayoutData& OutData)
{
	FSWGIffChunk InlyForm, VersionForm;
	if (!Reader.FindForm(SWG_IFF_TAG('I','N','L','Y'), InlyForm) || !Reader.FindChildForm(InlyForm, SWG_IFF_TAG('0','0','0','0'), VersionForm))
	{
		UE_LOG(LogTemp, Error, TEXT("FSWGInteriorLayoutReader: not a FORM INLY/0000"));
		return false;
	}

	for (const FSWGIffChunk& Chunk : Reader.ReadChildren(VersionForm))
	{
		if (Chunk.IsForm() || Chunk.Tag != SWG_IFF_TAG('N','O','D','E'))
		{
			continue;
		}

		FSWGIFFChunkReader NodeReader(Chunk, Reader);
		FSWGInteriorLayoutNode Node;
		if (!NodeReader.ReadTerminiatedString(Node.TemplatePath)
			|| !NodeReader.ReadTerminiatedString(Node.CellName)
			|| !NodeReader.ReadTransform<FTransform, float>(Node.Transform, SWGWorldScale))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGInteriorLayoutReader: malformed NODE (%d bytes) — skipped"), Chunk.DataSize);
			continue;
		}

		OutData.Nodes.Add(MoveTemp(Node));
	}

	return true;
}
