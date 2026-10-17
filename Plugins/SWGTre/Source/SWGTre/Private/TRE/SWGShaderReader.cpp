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
	if (Tag == TEXT("REP0")) return ESWGShaderTextureUsage::Replaceable;
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

	// SSHT's version-tagged wrapper form isn't always "0000" (same
	// version-drift pattern ResolveShaderDiffuseTexturePath already works
	// around) — take whichever single FORM child is actually present. TXMS
	// and the effect-name NAME chunk are both direct children of this
	// wrapper, so scoping to it (rather than a global FindForm/FindChildChunk
	// from the SSHT root) avoids picking up the unrelated NAME chunks nested
	// inside each individual TXM entry (those hold texture paths, not the
	// effect name).
	const TArray<FSWGIffChunk> SshtChildForms = Reader.FindChildForms(SshtForm);
	if (SshtChildForms.Num() == 0)
	{
		return false;
	}
	const FSWGIffChunk& Form0000 = SshtChildForms[0];

	FSWGIffChunk EffectNameChunk;
	if (Reader.FindChildChunk(Form0000, SWGIffTags::Name, EffectNameChunk))
	{
		OutShader.EffectName = ReadCString(Reader.GetChunkData(EffectNameChunk), Reader.GetChunkSize(EffectNameChunk));
		OutShader.EffectName.ReplaceInline(TEXT("\\"), TEXT("/"));
	}

	FSWGIffChunk InlineEffect;
	OutShader.bHasInlineEffect = Reader.FindChildForm(Form0000, SWG_IFF_TAG('E','F','C','T'), InlineEffect);

	FSWGIffChunk TxmsForm;
	if (!Reader.FindChildForm(Form0000, SWG_IFF_TAG('T','X','M','S'), TxmsForm))
	{
		return !OutShader.EffectName.IsEmpty() || OutShader.bHasInlineEffect;
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

	// FORM TCSS > CHUNK 0000: repeated [4-byte reversed tag][uint8 uvSet].
	// e.g. con_gen_inorganic_minerals_arsd24.sht holds MAIN=0, REP0=1.
	// Absent on single-texture shaders, where UV0 for everything is correct.
	FSWGIffChunk TcssForm;
	if (Reader.FindChildForm(Form0000, SWG_IFF_TAG('T','C','S','S'), TcssForm))
	{
		for (const FSWGIffChunk& Child : Reader.ReadChildren(TcssForm))
		{
			if (Child.IsForm())
			{
				continue;
			}

			const uint8* Data = Reader.GetChunkData(Child);
			const int32 Size = Reader.GetChunkSize(Child);
			for (int32 Offset = 0; Offset + 5 <= Size; Offset += 5)
			{
				const FString Tag = ReadReversedTag(Data + Offset);
				const uint8 UvSet = Data[Offset + 4];
				for (FSWGShaderTexture& Texture : OutShader.Textures)
				{
					if (Texture.Tag == Tag)
					{
						Texture.TexCoordSet = UvSet;
					}
				}
			}
		}
	}

	// FORM ARVS > CHUNK 0000: repeated [4-byte reversed tag][uint8 reference].
	FSWGIffChunk ArvsForm;
	if (Reader.FindChildForm(Form0000, SWG_IFF_TAG('A','R','V','S'), ArvsForm))
	{
		for (const FSWGIffChunk& Child : Reader.ReadChildren(ArvsForm))
		{
			if (Child.IsForm())
			{
				continue;
			}

			const uint8* Data = Reader.GetChunkData(Child);
			const int32 Size = Reader.GetChunkSize(Child);
			for (int32 Offset = 0; Offset + 5 <= Size; Offset += 5)
			{
				OutShader.AlphaReferenceValues.Add(ReadReversedTag(Data + Offset), Data[Offset + 4]);
			}
		}
	}

	// FORM TSNS > CHUNK 0000: repeated [4-byte reversed tag][u1 v1 u2 v2].
	FSWGIffChunk TsnsForm;
	if (Reader.FindChildForm(Form0000, SWG_IFF_TAG('T','S','N','S'), TsnsForm))
	{
		for (const FSWGIffChunk& Child : Reader.ReadChildren(TsnsForm))
		{
			if (Child.IsForm())
			{
				continue;
			}

			const uint8* Data = Reader.GetChunkData(Child);
			const int32 Size = Reader.GetChunkSize(Child);
			for (int32 Offset = 0; Offset + 20 <= Size; Offset += 20)
			{
				FSWGShaderScroll Scroll;
				Scroll.Tag = ReadReversedTag(Data + Offset);
				FMemory::Memcpy(&Scroll.U1, Data + Offset + 4, 4 * sizeof(float));
				OutShader.Scrolls.Add(MoveTemp(Scroll));
			}
		}
	}

	return OutShader.Textures.Num() > 0;
}

int32 FSWGShaderAnimation::GetFrameAt(float TimeSeconds) const
{
	if (Timing != ESWGShaderTiming::DeltaTime || SecondsPerFrame <= 0.f || FrameCount <= 0)
	{
		return 0;
	}
	return FMath::FloorToInt(TimeSeconds / SecondsPerFrame) % FrameCount;
}

bool FSWGShaderReader::ReadAnimation(const FSWGIffReader& Reader, FSWGShaderAnimation& OutAnimation)
{
	OutAnimation = FSWGShaderAnimation();
	if (!Reader.IsValid())
	{
		return false;
	}

	FSWGIffChunk RootForm;
	if (Reader.FindForm(SWG_IFF_TAG('S','W','T','S'), RootForm))
	{
		OutAnimation.bSwitchesShaders = false;
	}
	else if (Reader.FindForm(SWG_IFF_TAG('S','W','S','H'), RootForm))
	{
		OutAnimation.bSwitchesShaders = true;
	}
	else
	{
		return false;
	}

	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(RootForm);
	if (VersionForms.IsEmpty())
	{
		return false;
	}

	for (const FSWGIffChunk& Child : Reader.ReadChildren(VersionForms[0]))
	{
		if (Child.IsForm())
		{
			FSWGIffChunk Data;
			if (!Reader.FindChildChunk(Child, SWG_IFF_TAG('0','0','0','0'), Data) || Data.DataSize < 4)
			{
				return false;
			}
			const uint8* Bytes = Reader.GetChunkData(Data);
			FMemory::Memcpy(&OutAnimation.FrameCount, Bytes, sizeof(int32));

			if (Child.FormType == SWG_IFF_TAG('D','T','S','T') && Data.DataSize == 8)
			{
				OutAnimation.Timing = ESWGShaderTiming::DeltaTime;
				FMemory::Memcpy(&OutAnimation.SecondsPerFrame, Bytes + 4, sizeof(float));
			}
			else if (Child.FormType == SWG_IFF_TAG('D','F','S','T') && Data.DataSize == 4)
			{
				OutAnimation.Timing = ESWGShaderTiming::DeltaFrame;
			}
			else if (Child.FormType == SWG_IFF_TAG('D','R','F','S') && Data.DataSize == 4)
			{
				OutAnimation.Timing = ESWGShaderTiming::RandomFrame;
			}
			else if (Child.FormType == SWG_IFF_TAG('D','R','T','S') && Data.DataSize == 12)
			{
				OutAnimation.Timing = ESWGShaderTiming::RandomTime;
				FMemory::Memcpy(&OutAnimation.RandomTimeA, Bytes + 4, sizeof(float));
				FMemory::Memcpy(&OutAnimation.RandomTimeB, Bytes + 8, sizeof(float));
			}
			else
			{
				return false;
			}
			continue;
		}

		const uint8* Bytes = Reader.GetChunkData(Child);
		if (Child.Tag == SWGIffTags::Name)
		{
			FString Path = ReadCString(Bytes, Child.DataSize);
			Path.ReplaceInline(TEXT("\\"), TEXT("/"));
			if (OutAnimation.bSwitchesShaders)
			{
				OutAnimation.Shaders.Add(MoveTemp(Path));
			}
			else
			{
				OutAnimation.BaseShaderPath = MoveTemp(Path);
			}
		}
		else if (Child.Tag == SWG_IFF_TAG('T','E','X','T') && Child.DataSize > 4)
		{
			FSWGShaderAnimationTexture Frame;
			Frame.Tag = ReadReversedTag(Bytes);
			Frame.TexturePath = ReadCString(Bytes + 4, Child.DataSize - 4);
			Frame.TexturePath.ReplaceInline(TEXT("\\"), TEXT("/"));
			OutAnimation.Textures.Add(MoveTemp(Frame));
		}
	}

	return OutAnimation.Timing != ESWGShaderTiming::None && OutAnimation.GetDeclaredFrames() > 0;
}

ESWGEffectBlendMode FSWGEffectPass::GetBlendMode() const
{
	if (!bAlphaBlend)
	{
		return bAlphaTest ? ESWGEffectBlendMode::Masked : ESWGEffectBlendMode::Opaque;
	}
	if (BlendOperation == 2)
	{
		return ESWGEffectBlendMode::Subtractive;
	}
	if (BlendOperation != 0)
	{
		return ESWGEffectBlendMode::Other;
	}
	if (DestFactor == ESWGEffectBlendFactor::One)
	{
		return ESWGEffectBlendMode::Additive;
	}
	if (SourceFactor == ESWGEffectBlendFactor::SrcAlpha && DestFactor == ESWGEffectBlendFactor::InvSrcAlpha)
	{
		return ESWGEffectBlendMode::Translucent;
	}
	if (DestFactor == ESWGEffectBlendFactor::SrcColor || SourceFactor == ESWGEffectBlendFactor::DestColor)
	{
		return ESWGEffectBlendMode::Modulate;
	}
	return ESWGEffectBlendMode::Other;
}

ESWGEffectBlendMode FSWGEffectData::GetBlendMode() const
{
	if (Implementations.IsEmpty() || Implementations[0].Passes.IsEmpty())
	{
		return ESWGEffectBlendMode::Opaque;
	}
	return Implementations[0].Passes[0].GetBlendMode();
}

bool FSWGShaderReader::ReadEffect(const FSWGIffReader& Reader, FSWGEffectData& OutEffect)
{
	OutEffect = FSWGEffectData();

	FSWGIffChunk EfctForm;
	if (!Reader.IsValid() || !Reader.FindForm(SWG_IFF_TAG('E','F','C','T'), EfctForm))
	{
		return false;
	}
	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(EfctForm);
	if (VersionForms.IsEmpty())
	{
		return false;
	}
	OutEffect.Version = FCString::Atoi(*VersionForms[0].FormType.ToString());

	for (const FSWGIffChunk& ImplForm : Reader.ReadChildren(VersionForms[0]))
	{
		if (!ImplForm.IsForm() || ImplForm.FormType != SWG_IFF_TAG('I','M','P','L'))
		{
			continue;
		}
		const TArray<FSWGIffChunk> ImplVersions = Reader.FindChildForms(ImplForm);
		if (ImplVersions.IsEmpty())
		{
			return false;
		}

		FSWGEffectImplementation Implementation;
		Implementation.Version = FCString::Atoi(*ImplVersions[0].FormType.ToString());
		for (const FSWGIffChunk& Child : Reader.ReadChildren(ImplVersions[0]))
		{
			const uint8* Bytes = Reader.GetChunkData(Child);
			if (!Child.IsForm())
			{
				if (Child.Tag == SWG_IFF_TAG('S','C','A','P'))
				{
					Implementation.Capabilities.Append(Bytes, Child.DataSize);
				}
				else if (Child.Tag == SWG_IFF_TAG('O','P','T','N'))
				{
					Implementation.Options.Append(Bytes, Child.DataSize);
				}
				else if (Child.Tag == SWGIffTags::Data && Child.DataSize >= 1)
				{
					Implementation.Level = Bytes[0];
					if (Child.DataSize >= 5)
					{
						Implementation.Phase = ReadReversedTag(Bytes + 1);
					}
					for (int32 FlagIndex = 0; FlagIndex < 2 && 5 + FlagIndex < Child.DataSize; ++FlagIndex)
					{
						Implementation.Flags[FlagIndex] = Bytes[5 + FlagIndex];
					}
				}
				continue;
			}

			if (Child.FormType != SWG_IFF_TAG('P','A','S','S'))
			{
				continue;
			}
			const TArray<FSWGIffChunk> PassVersions = Reader.FindChildForms(Child);
			if (PassVersions.IsEmpty())
			{
				return false;
			}

			FSWGEffectPass Pass;
			Pass.Version = FCString::Atoi(*PassVersions[0].FormType.ToString());
			for (const FSWGIffChunk& PassChild : Reader.ReadChildren(PassVersions[0]))
			{
				if (!PassChild.IsForm())
				{
					if (PassChild.Tag == SWGIffTags::Data)
					{
						Pass.RawData.Append(Reader.GetChunkData(PassChild), PassChild.DataSize);
					}
					continue;
				}

				if (PassChild.FormType == SWG_IFF_TAG('P','V','S','H'))
				{
					FSWGIffChunk Program;
					if (Reader.FindChildChunk(PassChild, SWG_IFF_TAG('0','0','0','0'), Program))
					{
						Pass.VertexProgram = ReadCString(Reader.GetChunkData(Program), Program.DataSize);
					}
				}
				else if (PassChild.FormType == SWG_IFF_TAG('P','P','S','H'))
				{
					for (const FSWGIffChunk& PixelVersion : Reader.FindChildForms(PassChild))
					{
						FSWGIffChunk Program;
						if (Reader.FindChildChunk(PixelVersion, SWGIffTags::Data, Program) && Program.DataSize > 1)
						{
							// DATA = [byte][path]; the leading byte is 1 in every file.
							Pass.PixelProgram = ReadCString(Reader.GetChunkData(Program) + 1, Program.DataSize - 1);
						}
					}
				}
			}

			if (Pass.Version >= 5 && Pass.RawData.Num() >= 18)
			{
				const TArray<uint8>& Data = Pass.RawData;
				Pass.FixedFunctionStages = Data[0];
				Pass.FogMode = Data[2];
				Pass.bDepthTest = Data[4] != 0;
				Pass.bDepthWrite = Data[5] != 0;
				Pass.DepthCompare = Data[6];
				Pass.bAlphaBlend = Data[7] != 0;
				Pass.BlendOperation = Data[8];
				Pass.SourceFactor = static_cast<ESWGEffectBlendFactor>(FMath::Min<uint8>(Data[9], (uint8)ESWGEffectBlendFactor::SrcAlphaSat));
				Pass.DestFactor = static_cast<ESWGEffectBlendFactor>(FMath::Min<uint8>(Data[10], (uint8)ESWGEffectBlendFactor::SrcAlphaSat));
				Pass.bAlphaTest = Data[11] != 0;
				if (Pass.bAlphaTest)
				{
					Pass.AlphaReferenceTag = ReadReversedTag(Data.GetData() + 12);
				}
				Pass.AlphaCompare = Data[16];
				Pass.ColorWriteMask = Data[17];
			}
			else if (Pass.RawData.Num() >= 1)
			{
				Pass.FixedFunctionStages = Pass.RawData[0];
			}
			Implementation.Passes.Add(MoveTemp(Pass));
		}
		OutEffect.Implementations.Add(MoveTemp(Implementation));
	}

	return !OutEffect.Implementations.IsEmpty();
}

float FSWGEffectRenderStates::ResolveAlphaThreshold(const FSWGShaderData& Shader, float Fallback) const
{
	if (AlphaReferenceLiteral.IsSet())
	{
		return AlphaReferenceLiteral.GetValue() / 255.0f;
	}
	if (const uint8* Reference = Shader.AlphaReferenceValues.Find(AlphaReferenceTag))
	{
		return *Reference / 255.0f;
	}
	return Fallback;
}

bool FSWGShaderReader::ReadEffectRenderStates(const FSWGIffReader& Reader, FSWGEffectRenderStates& OutStates)
{
	OutStates = FSWGEffectRenderStates();
	if (!Reader.IsValid())
	{
		return false;
	}

	FSWGIffChunk EfctForm;
	if (!Reader.FindForm(SWG_IFF_TAG('E','F','C','T'), EfctForm))
	{
		return false;
	}
	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(EfctForm);
	if (VersionForms.IsEmpty())
	{
		return false;
	}

	bool bFoundPass = false;
	bool bFirstPass = true;
	for (const FSWGIffChunk& ImplForm : Reader.ReadChildren(VersionForms[0]))
	{
		if (!ImplForm.IsForm() || ImplForm.FormType != SWG_IFF_TAG('I','M','P','L')) continue;
		for (const FSWGIffChunk& ImplVersion : Reader.FindChildForms(ImplForm))
		{
			// Only the base pass decides the surface: a_2blend_*/decal/dirt
			// effects follow an opaque base with blended overlay passes, and
			// reading those made every skin and machinery shader a cutout.
			bool bReadBasePass = false;
			for (const FSWGIffChunk& PassForm : Reader.ReadChildren(ImplVersion))
			{
				if (bReadBasePass) break;
				if (!PassForm.IsForm() || PassForm.FormType != SWG_IFF_TAG('P','A','S','S')) continue;
				for (const FSWGIffChunk& PassVersion : Reader.FindChildForms(PassForm))
				{
					if (bReadBasePass) break;
					FSWGIffChunk DataChunk;
					if (!Reader.FindChildChunk(PassVersion, SWGIffTags::Data, DataChunk) || DataChunk.DataSize < 18)
					{
						continue;
					}
					const uint8* Data = Reader.GetChunkData(DataChunk);
					bFoundPass = true;
					bReadBasePass = true;

					// Depth write follows the best (first-listed) implementation only.
					if (bFirstPass)
					{
						OutStates.bWritesDepth = Data[5] != 0;
						bFirstPass = false;
					}
					OutStates.bAlphaBlend |= Data[7] != 0;
					if (Data[11] != 0)
					{
						OutStates.bAlphaTest = true;
						const FString Tag = ReadReversedTag(Data + 12);
						if (OutStates.AlphaReferenceTag.IsEmpty() && !OutStates.AlphaReferenceLiteral.IsSet())
						{
							if (Tag.Len() == 4 && Tag[0] == TEXT('A') && FChar::IsDigit(Tag[1]) && FChar::IsDigit(Tag[2]) && FChar::IsDigit(Tag[3]))
							{
								OutStates.AlphaReferenceLiteral = (uint8)FMath::Clamp(FCString::Atoi(*Tag.Mid(1)), 0, 255);
							}
							else
							{
								OutStates.AlphaReferenceTag = Tag;
							}
						}
					}
				}
			}
		}
	}

	return bFoundPass;
}
