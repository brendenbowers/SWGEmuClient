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

bool FSWGObjectTemplateReader::FindIntField(const FSWGIffReader& Reader, const TCHAR* Key, int32& OutValue)
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

		uint8 HasValue = 0, ValueType = 0;
		if (!ChunkReader.ReadValueLE(HasValue) || HasValue == 0)
		{
			return false; // unset in this layer
		}
		// Numeric params mark a plain literal with 0x20 (every int/float field
		// in shared_static_base.iff), unlike strings' 0x01; ranges and die
		// rolls use other values and aren't a single number to return.
		if (!ChunkReader.ReadValueLE(ValueType) || (ValueType != 0x20 && ValueType != 0x01))
		{
			return false;
		}
		return ChunkReader.ReadValueLE(OutValue);
	}
	return false;
}

bool FSWGObjectTemplateReader::FindCreatureFloatArrayField(const FSWGIffReader& Reader, const TCHAR* Key, int32 Index, float& OutValue)
{
	// FORM SCOT's children are its DERV, its own version form (the creature
	// fields) and the nested FORM STOT holding the lower layers.
	FSWGIffChunk Scot;
	if (!Reader.FindForm(SWG_IFF_TAG('S','C','O','T'), Scot))
	{
		return false;
	}
	const FSWGIffChunk* DataForm = nullptr;
	const TArray<FSWGIffChunk> Children = Reader.ReadChildren(Scot);
	for (const FSWGIffChunk& Child : Children)
	{
		if (Child.IsForm() && Child.FormType != SWG_IFF_TAG('D','E','R','V') && Child.FormType != SWG_IFF_TAG('S','T','O','T'))
		{
			DataForm = &Child;
			break;
		}
	}
	if (!DataForm)
	{
		return false;
	}

	for (const FSWGIffChunk& Child : Reader.FindAllChildChunks(*DataForm, SWG_IFF_TAG('X','X','X','X')))
	{
		FSWGIFFChunkReader ChunkReader(Child, Reader);
		FString ChunkKey;
		if (!ChunkReader.ReadTerminiatedString(ChunkKey) || !ChunkKey.Equals(Key))
		{
			continue;
		}

		int32 Count = 0;
		if (!ChunkReader.ReadValueLE(Count) || Index < 0 || Index >= Count)
		{
			return false;
		}
		for (int32 Entry = 0; Entry < Count; ++Entry)
		{
			uint8 HasValue = 0, ValueType = 0;
			if (!ChunkReader.ReadValueLE(HasValue) || !ChunkReader.ReadValueLE(ValueType))
			{
				return false;
			}
			float Value = 0.0f;
			if (HasValue != 0 && !ChunkReader.ReadValueLE(Value))
			{
				return false;
			}
			if (Entry == Index)
			{
				OutValue = Value;
				return HasValue != 0;
			}
		}
		return false;
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

namespace
{
	/** Opens the named XXXX chunk of FormType's versioned data form and reads past the has-value flag. */
	bool OpenFormField(const FSWGIffReader& Reader, FSWGIffTag FormType, const TCHAR* Key, FSWGIffChunk& OutChunk)
	{
		FSWGIffChunk Form;
		if (!Reader.FindForm(FormType, Form))
		{
			return false;
		}

		for (const FSWGIffChunk& DataForm : Reader.ReadChildren(Form))
		{
			if (!DataForm.IsForm() || DataForm.FormType == SWG_IFF_TAG('D','E','R','V'))
			{
				continue;
			}

			for (const FSWGIffChunk& Child : Reader.FindAllChildChunks(DataForm, SWG_IFF_TAG('X','X','X','X')))
			{
				FSWGIFFChunkReader ChunkReader(Child, Reader);
				FString ChunkKey;
				if (ChunkReader.ReadTerminiatedString(ChunkKey) && ChunkKey.Equals(Key))
				{
					OutChunk = Child;
					return true;
				}
			}
			return false;
		}
		return false;
	}
}

bool FSWGObjectTemplateReader::FindStringFieldInForm(const FSWGIffReader& Reader, FSWGIffTag FormType, const TCHAR* Key, FString& OutValue)
{
	FSWGIffChunk Chunk;
	if (!OpenFormField(Reader, FormType, Key, Chunk))
	{
		return false;
	}

	FSWGIFFChunkReader ChunkReader(Chunk, Reader);
	FString Skipped;
	uint8 HasValue = 0;
	ChunkReader.ReadTerminiatedString(Skipped);
	return ChunkReader.ReadValueLE(HasValue) && HasValue != 0 && ChunkReader.ReadTerminiatedString(OutValue) && !OutValue.IsEmpty();
}

bool FSWGObjectTemplateReader::FindIntFieldInForm(const FSWGIffReader& Reader, FSWGIffTag FormType, const TCHAR* Key, int32& OutValue)
{
	FSWGIffChunk Chunk;
	if (!OpenFormField(Reader, FormType, Key, Chunk))
	{
		return false;
	}

	FSWGIFFChunkReader ChunkReader(Chunk, Reader);
	FString Skipped;
	uint8 HasValue = 0, ValueType = 0;
	ChunkReader.ReadTerminiatedString(Skipped);
	return ChunkReader.ReadValueLE(HasValue) && HasValue != 0 && ChunkReader.ReadValueLE(ValueType) && ValueType == 0x20 && ChunkReader.ReadValueLE(OutValue);
}
