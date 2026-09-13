#include "TRE/SWGObjectTemplateReader.h"
#include "TRE/SWGIFFChunkReader.h"

namespace
{
	// The one non-DERV FORM child of SHOT — the versioned data form holding
	// this layer's XXXX fields.
	bool FindShotDataForm(const FSWGIffReader& Reader, FSWGIffChunk& OutShot, FSWGIffChunk& OutDataForm)
	{
		if (!Reader.FindForm(SWG_IFF_TAG('S','H','O','T'), OutShot))
		{
			return false;
		}
		for (const FSWGIffChunk& Child : Reader.ReadChildren(OutShot))
		{
			if (Child.IsForm() && Child.FormType != SWG_IFF_TAG('D','E','R','V'))
			{
				OutDataForm = Child;
				return true;
			}
		}
		return false;
	}
}

bool FSWGObjectTemplateReader::FindStringIdField(const FSWGIffReader& Reader, const TCHAR* Key, FString& OutTable, FString& OutText)
{
	FSWGIffChunk Shot, DataForm;
	if (!FindShotDataForm(Reader, Shot, DataForm))
	{
		return false;
	}

	for (const FSWGIffChunk& Child : Reader.FindAllChildChunks(DataForm, SWG_IFF_TAG('X','X','X','X')))
	{
		FSWGIFFChunkReader ChunkReader(Child, Reader);
		FString ChunkKey;
		if (!ChunkReader.ReadTerminiatedString(ChunkKey) || !ChunkKey.Equals(Key))
		{
			continue;
		}

		uint8 HasValue = 0, TableType = 0, TextType = 0;
		if (!ChunkReader.ReadValueLE(HasValue) || HasValue == 0)
		{
			return false; // unset in this layer
		}

		// Each half is a StringParam: type byte (1 = literal) then the string.
		// Anything but a literal (weighted lists, template refs) isn't a name.
		if (!ChunkReader.ReadValueLE(TableType) || TableType != 1 || !ChunkReader.ReadTerminiatedString(OutTable)) return false;
		if (!ChunkReader.ReadValueLE(TextType)  || TextType  != 1 || !ChunkReader.ReadTerminiatedString(OutText))  return false;
		return !OutTable.IsEmpty() && !OutText.IsEmpty();
	}
	return false;
}

bool FSWGObjectTemplateReader::FindDervParentPath(const FSWGIffReader& Reader, FString& OutParentPath)
{
	FSWGIffChunk Shot, Derv, Xxxx;
	if (!Reader.FindForm(SWG_IFF_TAG('S','H','O','T'), Shot)
		|| !Reader.FindChildForm(Shot, SWG_IFF_TAG('D','E','R','V'), Derv)
		|| !Reader.FindChildChunk(Derv, SWG_IFF_TAG('X','X','X','X'), Xxxx))
	{
		return false;
	}

	FSWGIFFChunkReader ChunkReader(Xxxx, Reader);
	return ChunkReader.ReadTerminiatedString(OutParentPath) && !OutParentPath.IsEmpty();
}
