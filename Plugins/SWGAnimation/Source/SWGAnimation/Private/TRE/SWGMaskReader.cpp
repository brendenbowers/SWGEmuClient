#include "TRE/SWGMaskReader.h"

#include "TRE/SWGIffReader.h"
#include "TRE/SWGIffTags.h"
#include "TRE/SWGIFFChunkReader.h"

bool FSWGMaskReader::ReadMask(const FSWGIffReader& Reader, TArray<FString>& OutBoneNames)
{
	OutBoneNames.Reset();

	FSWGIffChunk MaskForm;
	if (!Reader.IsValid() || !Reader.FindForm(SWG_IFF_TAG('X', 'F', 'M', 'S'), MaskForm))
	{
		return false;
	}

	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(MaskForm);
	FSWGIffChunk BoneChunk;
	if (VersionForms.Num() == 0 || !Reader.FindChildChunk(VersionForms[0], SWG_IFF_TAG('I', 'N', 'G', 'R'), BoneChunk))
	{
		return false;
	}

	FSWGIFFChunkReader BoneReader(BoneChunk, Reader);
	const uint16 BoneCount = BoneReader.ReadValueLE<uint16>();
	for (uint16 Index = 0; Index < BoneCount && !BoneReader.AtEnd(); ++Index)
	{
		FString BoneName;
		if (!BoneReader.ReadTerminiatedString(BoneName))
		{
			break;
		}
		OutBoneNames.Add(MoveTemp(BoneName));
	}
	return OutBoneNames.Num() > 0;
}
