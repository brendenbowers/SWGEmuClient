#include "TRE/SWGShaderReader.h"
#include "TRE/SWGIffTags.h"

namespace
{
	FString ReadCString(const uint8* Data, int32 Size)
	{
		int32 Length = 0;
		while (Length < Size && Data[Length] != 0)
		{
			++Length;
		}
		return FString::ConstructFromPtrSize(reinterpret_cast<const ANSICHAR*>(Data), Length);
	}

	FString ReadReversedTag(const uint8* Data)
	{
		return FString::Printf(TEXT("%c%c%c%c"),
			static_cast<TCHAR>(Data[3]), static_cast<TCHAR>(Data[2]),
			static_cast<TCHAR>(Data[1]), static_cast<TCHAR>(Data[0]));
	}
}

const FSWGShaderTexture* FSWGShaderData::FindTexture(ESWGShaderTextureUsage Usage) const
{
	return Textures.FindByPredicate([Usage](const FSWGShaderTexture& Texture)
		{
			return Texture.Usage == Usage && !Texture.VirtualPath.IsEmpty();
		});
}

ESWGShaderTextureUsage FSWGShaderReader::ClassifyTextureTag(const FString& Tag)
{
	if (Tag == TEXT("MAIN")) return ESWGShaderTextureUsage::Diffuse;
	// NRML is the normal terrain/object tag. CNRM is the customization-aware
	// creature normal slot used by shaders such as wke_m_body.sht.
	if (Tag == TEXT("NRML") || Tag == TEXT("CNRM")) return ESWGShaderTextureUsage::Normal;
	if (Tag == TEXT("SPEC")) return ESWGShaderTextureUsage::Specular;
	if (Tag == TEXT("DOT3")) return ESWGShaderTextureUsage::Lightmap;
	return ESWGShaderTextureUsage::Unknown;
}

bool FSWGShaderReader::ReadShader(const FSWGIffReader& Reader, FSWGShaderData& OutShader)
{
	OutShader = FSWGShaderData();
	if (!Reader.IsValid())
	{
		return false;
	}

	FSWGIffChunk SshtForm;
	if (!Reader.FindForm(SWG_IFF_TAG('S','S','H','T'), SshtForm))
	{
		return false;
	}

	FSWGIffChunk TxmsForm;
	if (!Reader.FindForm(SWG_IFF_TAG('T','X','M','S'), TxmsForm))
	{
		return false;
	}

	for (const FSWGIffChunk& TxmForm : Reader.ReadChildren(TxmsForm))
	{
		if (!TxmForm.IsForm() || TxmForm.FormType != SWG_IFF_TAG('T','X','M',' '))
		{
			continue;
		}

		FSWGIffChunk DataChunk;
		FSWGIffChunk NameChunk;
		for (const FSWGIffChunk& VersionForm : Reader.ReadChildren(TxmForm))
		{
			if (!VersionForm.IsForm()) continue;
			for (const FSWGIffChunk& Child : Reader.ReadChildren(VersionForm))
			{
				if (!Child.IsForm() && Child.Tag == SWGIffTags::Data) DataChunk = Child;
				if (!Child.IsForm() && Child.Tag == SWGIffTags::Name) NameChunk = Child;
			}
		}

		if (DataChunk.DataSize < 4 || NameChunk.DataSize <= 0)
		{
			continue;
		}

		const uint8* Data = Reader.GetChunkData(DataChunk);
		FSWGShaderTexture Texture;
		Texture.Tag = ReadReversedTag(Data);
		Texture.Usage = ClassifyTextureTag(Texture.Tag);
		Texture.VirtualPath = ReadCString(Reader.GetChunkData(NameChunk), NameChunk.DataSize);
		Texture.VirtualPath.ReplaceInline(TEXT("\\"), TEXT("/"));

		// Exporter's sht_parser confirms DATA after the reversed tag is:
		// placeholder, address U/V/W, then mip/min/mag filter modes.
		if (DataChunk.DataSize >= 11)
		{
			Texture.AddressU = Data[5];
			Texture.AddressV = Data[6];
			Texture.AddressW = Data[7];
			Texture.MipFilter = Data[8];
			Texture.MinFilter = Data[9];
			Texture.MagFilter = Data[10];
		}

		OutShader.Textures.Add(MoveTemp(Texture));
	}

	return OutShader.Textures.Num() > 0;
}
