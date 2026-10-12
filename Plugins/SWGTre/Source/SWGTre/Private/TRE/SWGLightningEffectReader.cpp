#include "TRE/SWGLightningEffectReader.h"

#include "TRE/SWGIFFChunkReader.h"

bool FSWGLightningEffectReader::Read(const FSWGIffReader& Reader, FSWGLightningEffect& OutEffect)
{
	OutEffect = FSWGLightningEffect();

	FSWGIffChunk LefxForm;
	if (!Reader.IsValid() || !Reader.FindForm(SWG_IFF_TAG('L', 'E', 'F', 'X'), LefxForm))
	{
		return false;
	}
	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(LefxForm);
	if (VersionForms.IsEmpty())
	{
		return false;
	}
	OutEffect.Version = FCString::Atoi(*VersionForms[0].FormType.ToString());
	if (OutEffect.Version > 2)
	{
		return false;
	}

	FSWGIffChunk TextureForm, DataChunk;
	if (!Reader.FindChildForm(VersionForms[0], SWG_IFF_TAG('P', 'T', 'E', 'X'), TextureForm)
		|| !FSWGFxCommonReader::ReadTexture(Reader, TextureForm, OutEffect.Texture)
		|| !Reader.FindChildChunk(VersionForms[0], SWG_IFF_TAG('0', '0', '0', '0'), DataChunk))
	{
		return false;
	}

	for (const FSWGIffChunk& Child : Reader.ReadChildren(VersionForms[0]))
	{
		if (Child.IsForm() && Child.FormType == SWG_IFF_TAG('W', 'V', 'F', 'M'))
		{
			FSWGWaveform Curve;
			if (!FSWGFxCommonReader::ReadWaveform(Reader, Child, Curve))
			{
				return false;
			}
			OutEffect.Curves.Add(MoveTemp(Curve));
		}
	}

	FSWGIFFChunkReader Chunk(DataChunk, Reader);
	if (!Chunk.ReadValueLE(OutEffect.LeadFloat) || !Chunk.ReadTerminiatedString(OutEffect.StartEffectPath)
		|| !Chunk.ReadTerminiatedString(OutEffect.EndEffectPath))
	{
		return false;
	}
	FSWGFxCommonReader::NormalizePath(OutEffect.StartEffectPath);
	FSWGFxCommonReader::NormalizePath(OutEffect.EndEffectPath);
	return Chunk.ReadRemainingBytes(OutEffect.RawTail);
}
