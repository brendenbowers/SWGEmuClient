#include "TRE/SWGTextureRenderer.h"

#include "TRE/SWGIFFChunkReader.h"
#include "TRE/SWGIffTags.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

namespace
{
	constexpr FSWGIffTag BtrtTag = SWG_IFF_TAG('B', 'T', 'R', 'T');
	constexpr FSWGIffTag Version0001 = SWG_IFF_TAG('0', '0', '0', '1');
	constexpr FSWGIffTag DestTag = SWG_IFF_TAG('D', 'E', 'S', 'T');
	constexpr FSWGIffTag ShtmTag = SWG_IFF_TAG('S', 'H', 'T', 'M');
	constexpr FSWGIffTag TxtsTag = SWG_IFF_TAG('T', 'X', 'T', 'S');
	constexpr FSWGIffTag VbsTag = SWG_IFF_TAG('V', 'B', 'S', ' ');
	constexpr FSWGIffTag VtxaTag = SWG_IFF_TAG('V', 'T', 'X', 'A');
	constexpr FSWGIffTag RcmsTag = SWG_IFF_TAG('R', 'C', 'M', 'S');
	constexpr FSWGIffTag SrscTag = SWG_IFF_TAG('S', 'R', 'S', 'C');
	constexpr FSWGIffTag CfbcTag = SWG_IFF_TAG('C', 'F', 'B', 'C');
	constexpr FSWGIffTag RtfcTag = SWG_IFF_TAG('R', 'T', 'F', 'C');
	constexpr FSWGIffTag PcmsTag = SWG_IFF_TAG('P', 'C', 'M', 'S');
	constexpr FSWGIffTag PcmdTag = SWG_IFF_TAG('P', 'C', 'M', 'D');
	constexpr FSWGIffTag TopsTag = SWG_IFF_TAG('T', 'O', 'P', 'S');
	constexpr FSWGIffTag FopsTag = SWG_IFF_TAG('F', 'O', 'P', 'S');
	constexpr FSWGIffTag CondTag = SWG_IFF_TAG('C', 'O', 'N', 'D');
	constexpr FSWGIffTag SstcTag = SWG_IFF_TAG('S', 'S', 'T', 'C');
	constexpr FSWGIffTag Sst1Tag = SWG_IFF_TAG('S', 'S', 'T', '1');
	constexpr FSWGIffTag Sst2Tag = SWG_IFF_TAG('S', 'S', 'T', '2');
	constexpr FSWGIffTag StfTag = SWG_IFF_TAG('S', 'T', 'F', ' ');
	constexpr FSWGIffTag StfaTag = SWG_IFF_TAG('S', 'T', 'F', 'A');
	constexpr FSWGIffTag NopTag = SWG_IFF_TAG('N', 'O', 'P', ' ');
	constexpr FSWGIffTag StfpTag = SWG_IFF_TAG('S', 'T', 'F', 'P');
	constexpr FSWGIffTag SkmgTag = SWG_IFF_TAG('S', 'K', 'M', 'G');
	constexpr FSWGIffTag TrtsTag = SWG_IFF_TAG('T', 'R', 'T', 'S');
	constexpr FSWGIffTag TrtTag = SWG_IFF_TAG('T', 'R', 'T', ' ');
	constexpr FSWGIffTag PsdtTag = SWG_IFF_TAG('P', 'S', 'D', 'T');

	FString NormalizePath(FString Path)
	{
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		return Path;
	}

	bool ReadCondition(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk, bool& Value, int32 Depth = 0)
	{
		if (Depth >= 16) return false;
		if (!Chunk.IsForm())
		{
			if (Chunk.DataSize != 0) return false;
			if (Chunk.Tag == SWG_IFF_TAG('T', 'R', 'U', 'E')) { Value = true; return true; }
			if (Chunk.Tag == SWG_IFF_TAG('F', 'A', 'L', 'S')) { Value = false; return true; }
			return false;
		}
		const bool bAnd = Chunk.FormType == SWG_IFF_TAG('A', 'N', 'D', ' ');
		if (!bAnd && Chunk.FormType != SWG_IFF_TAG('O', 'R', ' ', ' ')) return false;
		FSWGIffChunk Info;
		if (!Reader.FindChildChunk(Chunk, SWGIffTags::Info, Info)) return false;
		FSWGIFFChunkReader CountReader(Info, Reader);
		int32 Count;
		if (!CountReader.ReadValueLE(Count) || Count < 0) return false;
		Value = bAnd;
		int32 Seen = 0;
		for (const FSWGIffChunk& Child : Reader.ReadChildren(Chunk))
		{
			if (!Child.IsForm() && Child.Tag == SWGIffTags::Info) continue;
			bool ChildValue;
			if (!ReadCondition(Reader, Child, ChildValue, Depth + 1)) return false;
			Value = bAnd ? Value && ChildValue : Value || ChildValue;
			++Seen;
		}
		return Seen == Count;
	}

	uint32 ReadU32(const uint8* Bytes) { return Bytes[0] | (Bytes[1] << 8) | (Bytes[2] << 16) | ((uint32)Bytes[3] << 24); }

	FColor Expand565(uint16 Value)
	{
		const uint8 Red = (Value >> 11) & 31;
		const uint8 Green = (Value >> 5) & 63;
		const uint8 Blue = Value & 31;
		return FColor((Red << 3) | (Red >> 2), (Green << 2) | (Green >> 4), (Blue << 3) | (Blue >> 2), 255);
	}

	/** Decodes one 8-byte BC1 colour block into 16 pixels; bAllowAlpha makes colour 3 transparent in the 3-colour mode. */
	void DecodeColorBlock(const uint8* Block, bool bAllowAlpha, FColor Out[16])
	{
		const uint16 Color0 = Block[0] | (Block[1] << 8);
		const uint16 Color1 = Block[2] | (Block[3] << 8);
		FColor Palette[4];
		Palette[0] = Expand565(Color0);
		Palette[1] = Expand565(Color1);
		if (Color0 > Color1 || !bAllowAlpha)
		{
			Palette[2] = FColor((2 * Palette[0].R + Palette[1].R) / 3, (2 * Palette[0].G + Palette[1].G) / 3, (2 * Palette[0].B + Palette[1].B) / 3, 255);
			Palette[3] = FColor((Palette[0].R + 2 * Palette[1].R) / 3, (Palette[0].G + 2 * Palette[1].G) / 3, (Palette[0].B + 2 * Palette[1].B) / 3, 255);
		}
		else
		{
			Palette[2] = FColor((Palette[0].R + Palette[1].R) / 2, (Palette[0].G + Palette[1].G) / 2, (Palette[0].B + Palette[1].B) / 2, 255);
			Palette[3] = FColor(0, 0, 0, 0);
		}
		const uint32 Indices = ReadU32(Block + 4);
		for (int32 Pixel = 0; Pixel < 16; ++Pixel)
		{
			Out[Pixel] = Palette[(Indices >> (Pixel * 2)) & 3];
		}
	}
}

FLinearColor FSWGImage::SampleBilinear(float U, float V) const
{
	if (!IsValid())
	{
		return FLinearColor::White;
	}
	const float X = U * Width - 0.5f;
	const float Y = V * Height - 0.5f;
	const int32 X0 = FMath::FloorToInt(X);
	const int32 Y0 = FMath::FloorToInt(Y);
	const float FracX = X - X0;
	const float FracY = Y - Y0;
	auto Texel = [this](int32 TexelX, int32 TexelY)
	{
		const FColor& Color = Pixels[FMath::Clamp(TexelY, 0, Height - 1) * Width + FMath::Clamp(TexelX, 0, Width - 1)];
		return FLinearColor(Color.R / 255.f, Color.G / 255.f, Color.B / 255.f, Color.A / 255.f);
	};
	return FMath::Lerp(
		FMath::Lerp(Texel(X0, Y0), Texel(X0 + 1, Y0), FracX),
		FMath::Lerp(Texel(X0, Y0 + 1), Texel(X0 + 1, Y0 + 1), FracX),
		FracY);
}

bool FSWGDdsDecoder::Decode(const TArray<uint8>& Bytes, FSWGImage& OutImage)
{
	OutImage = FSWGImage();
	if (Bytes.Num() < 128 || ReadU32(Bytes.GetData()) != 0x20534444) // "DDS "
	{
		return false;
	}
	const uint8* Header = Bytes.GetData();
	const int32 Height = ReadU32(Header + 12);
	const int32 Width = ReadU32(Header + 16);
	const uint32 PixelFlags = ReadU32(Header + 80);
	const uint32 FourCc = ReadU32(Header + 84);
	if (Width <= 0 || Height <= 0 || Width > 8192 || Height > 8192)
	{
		return false;
	}
	const uint8* Data = Header + 128;
	const int32 DataSize = Bytes.Num() - 128;
	OutImage.Width = Width;
	OutImage.Height = Height;
	OutImage.Pixels.SetNumUninitialized(Width * Height);

	const bool bBlockFormat = (PixelFlags & 4) != 0; // DDPF_FOURCC
	if (bBlockFormat)
	{
		const uint32 Dxt1 = 0x31545844, Dxt3 = 0x33545844, Dxt5 = 0x35545844;
		if (FourCc != Dxt1 && FourCc != Dxt3 && FourCc != Dxt5)
		{
			return false;
		}
		const int32 BlockSize = FourCc == Dxt1 ? 8 : 16;
		const int32 BlocksX = (Width + 3) / 4;
		const int32 BlocksY = (Height + 3) / 4;
		if (DataSize < BlocksX * BlocksY * BlockSize)
		{
			return false;
		}
		for (int32 BlockY = 0; BlockY < BlocksY; ++BlockY)
		{
			for (int32 BlockX = 0; BlockX < BlocksX; ++BlockX)
			{
				const uint8* Block = Data + (BlockY * BlocksX + BlockX) * BlockSize;
				FColor Colors[16];
				DecodeColorBlock(FourCc == Dxt1 ? Block : Block + 8, FourCc == Dxt1, Colors);
				uint8 Alpha[16];
				for (int32 Pixel = 0; Pixel < 16; ++Pixel)
				{
					Alpha[Pixel] = 255;
				}
				if (FourCc == Dxt3)
				{
					for (int32 Pixel = 0; Pixel < 16; ++Pixel)
					{
						const uint8 Nibble = (Block[Pixel / 2] >> ((Pixel & 1) * 4)) & 15;
						Alpha[Pixel] = Nibble * 17;
					}
				}
				else if (FourCc == Dxt5)
				{
					uint8 Ramp[8] = { Block[0], Block[1] };
					if (Ramp[0] > Ramp[1])
					{
						for (int32 Step = 1; Step < 7; ++Step)
						{
							Ramp[Step + 1] = ((7 - Step) * Ramp[0] + Step * Ramp[1]) / 7;
						}
					}
					else
					{
						for (int32 Step = 1; Step < 5; ++Step)
						{
							Ramp[Step + 1] = ((5 - Step) * Ramp[0] + Step * Ramp[1]) / 5;
						}
						Ramp[6] = 0;
						Ramp[7] = 255;
					}
					uint64 Bits = 0;
					for (int32 Byte = 0; Byte < 6; ++Byte)
					{
						Bits |= (uint64)Block[2 + Byte] << (8 * Byte);
					}
					for (int32 Pixel = 0; Pixel < 16; ++Pixel)
					{
						Alpha[Pixel] = Ramp[(Bits >> (3 * Pixel)) & 7];
					}
				}
				for (int32 Pixel = 0; Pixel < 16; ++Pixel)
				{
					const int32 PixelX = BlockX * 4 + (Pixel & 3);
					const int32 PixelY = BlockY * 4 + (Pixel >> 2);
					if (PixelX < Width && PixelY < Height)
					{
						FColor Color = Colors[Pixel];
						if (FourCc != Dxt1)
						{
							Color.A = Alpha[Pixel];
						}
						OutImage.Pixels[PixelY * Width + PixelX] = Color;
					}
				}
			}
		}
		return true;
	}

	// Uncompressed: honour the channel masks.
	const uint32 BitCount = ReadU32(Header + 88);
	if (BitCount != 32 || DataSize < Width * Height * 4)
	{
		return false;
	}
	const uint32 RedMask = ReadU32(Header + 92);
	const uint32 GreenMask = ReadU32(Header + 96);
	const uint32 BlueMask = ReadU32(Header + 100);
	const uint32 AlphaMask = (PixelFlags & 1) ? ReadU32(Header + 104) : 0;
	auto Extract = [](uint32 Pixel, uint32 Mask) -> uint8
	{
		if (Mask == 0)
		{
			return 255;
		}
		const int32 Shift = FMath::CountTrailingZeros(Mask);
		return (uint8)((Pixel & Mask) >> Shift);
	};
	for (int32 Index = 0; Index < Width * Height; ++Index)
	{
		const uint32 Pixel = ReadU32(Data + Index * 4);
		OutImage.Pixels[Index] = FColor(Extract(Pixel, RedMask), Extract(Pixel, GreenMask), Extract(Pixel, BlueMask), Extract(Pixel, AlphaMask));
	}
	return true;
}

bool FSWGPalette::Parse(const TArray<uint8>& Bytes, FSWGPalette& OutPalette)
{
	OutPalette.Colors.Reset();
	// RIFF header, "PAL " form, "data" chunk: u32 size, u16 version, u16 count (at byte 22), then RGBF entries from byte 24.
	if (Bytes.Num() < 24 || FMemory::Memcmp(Bytes.GetData(), "RIFF", 4) != 0)
	{
		return false;
	}
	const int32 Offset = 24;
	const int32 Count = Bytes[22] | (Bytes[23] << 8);
	if (Count <= 0 || Offset + Count * 4 > Bytes.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const uint8* Entry = Bytes.GetData() + Offset + Index * 4;
		OutPalette.Colors.Add(FColor(Entry[0], Entry[1], Entry[2], 255));
	}
	return true;
}

bool FSWGTextureRenderer::Read(const FSWGIffReader& Reader, FSWGTextureRenderer& OutRenderer)
{
	OutRenderer = FSWGTextureRenderer();
	FSWGIffChunk Root, Body;
	if (!Reader.IsValid() || !Reader.FindForm(BtrtTag, Root) || !Reader.FindChildForm(Root, Version0001, Body))
	{
		return false;
	}

	FSWGIffChunk Chunk;
	if (!Reader.FindChildChunk(Body, DestTag, Chunk))
	{
		return false;
	}
	{
		FSWGIFFChunkReader Dest(Chunk, Reader);
		if (!Dest.ReadValueLE(OutRenderer.Width) || !Dest.ReadValueLE(OutRenderer.Height) || OutRenderer.Width <= 0 || OutRenderer.Height <= 0)
		{
			return false;
		}
	}

	FSWGIffChunk Form;
	if (Reader.FindChildForm(Body, ShtmTag, Form))
	{
		for (const FSWGIffChunk& Name : Reader.FindAllChildChunks(Form, SWGIffTags::Name))
		{
			FSWGIFFChunkReader NameReader(Name, Reader);
			OutRenderer.Shaders.Add(NormalizePath(NameReader.ReadTerminiatedString()));
		}
	}
	if (Reader.FindChildForm(Body, TxtsTag, Form) && Reader.FindChildChunk(Form, SWGIffTags::Info, Chunk))
	{
		FSWGIFFChunkReader Info(Chunk, Reader);
		int32 Count = 0;
		Info.ReadValueLE(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			OutRenderer.Textures.Add(NormalizePath(Info.ReadTerminiatedString()));
		}
	}
	if (Reader.FindChildForm(Body, VbsTag, Form))
	{
		for (const FSWGIffChunk& Vtxa : Reader.FindChildForms(Form))
		{
			FSWGIffChunk Version, Info, Data;
			TArray<FVertex>& Buffer = OutRenderer.VertexBuffers.AddDefaulted_GetRef();
			if (Vtxa.FormType != VtxaTag || Reader.ReadChildren(Vtxa).Num() == 0)
			{
				return false;
			}
			Version = Reader.ReadChildren(Vtxa)[0];
			if (!Reader.FindChildChunk(Version, SWGIffTags::Info, Info) || !Reader.FindChildChunk(Version, SWGIffTags::Data, Data))
			{
				return false;
			}
			FSWGIFFChunkReader InfoReader(Info, Reader);
			uint32 Format = 0;
			int32 VertexCount = 0;
			if (!InfoReader.ReadValueLE(Format) || !InfoReader.ReadValueLE(VertexCount) || Format != 0x1101 || VertexCount < 0 || VertexCount > Data.DataSize / 20)
			{
				return false; // only position + one 2D UV set is used by shipped renderers
			}
			FSWGIFFChunkReader DataReader(Data, Reader);
			for (int32 Vertex = 0; Vertex < VertexCount; ++Vertex)
			{
				FVertex Out;
				float Depth = 0.f;
				DataReader.ReadValueLE(Out.X);
				DataReader.ReadValueLE(Out.Y);
				DataReader.ReadValueLE(Depth);
				DataReader.ReadValueLE(Out.U);
				DataReader.ReadValueLE(Out.V);
				Buffer.Add(Out);
			}
		}
	}
	if (Reader.FindChildForm(Body, RcmsTag, Form))
	{
		for (const FSWGIffChunk& Command : Reader.ReadChildren(Form))
		{
			if (!Command.IsForm() && Command.Tag == SWGIffTags::Info) continue;
			if (!Command.IsForm() && Command.Tag == CfbcTag)
			{
				FSWGIFFChunkReader ClearReader(Command, Reader);
				uint32 Flags[6];
				for (uint32& Field : Flags) if (!ClearReader.ReadValueLE(Field)) return false;
				if (!ClearReader.AtEnd()) return false;
				// shortcut: CPU rendering has no depth/stencil buffers; add them if non-planar TRT assets appear.
				if (Flags[0] != 0)
				{
					FDraw Draw;
					Draw.bClearColor = true;
					Draw.ClearColor = Flags[1];
					OutRenderer.Draws.Add(Draw);
				}
				continue;
			}
			if (Command.FormType != SrscTag)
			{
				OutRenderer.bHasUnsupportedOps = true;
				continue;
			}
			FSWGIffChunk Info;
			if (!Reader.FindChildChunk(Command, SWGIffTags::Info, Info))
			{
				return false;
			}
			FSWGIFFChunkReader InfoReader(Info, Reader);
			int32 ShaderIndex = 0;
			InfoReader.ReadValueLE(ShaderIndex);
			for (const FSWGIffChunk& Primitive : Reader.FindAllChildChunks(Command, RtfcTag))
			{
				FSWGIFFChunkReader PrimitiveReader(Primitive, Reader);
				FDraw Draw;
				Draw.ShaderIndex = ShaderIndex;
				PrimitiveReader.ReadValueLE(Draw.VertexBuffer);
				OutRenderer.Draws.Add(Draw);
			}
		}
	}
	if (Reader.FindChildForm(Body, PcmsTag, Form))
	{
		for (const FSWGIffChunk& Pcmd : Reader.FindChildForms(Form))
		{
			if (Pcmd.FormType != PcmdTag) { OutRenderer.bHasUnsupportedOps = true; continue; }
			bool bTrue = true;
			FSWGIffChunk Condition;
			if (Reader.FindChildForm(Pcmd, CondTag, Condition))
			{
				const TArray<FSWGIffChunk> Children = Reader.ReadChildren(Condition);
				if (Children.Num() != 1 || !ReadCondition(Reader, Children[0], bTrue))
				{
					OutRenderer.bHasUnsupportedOps = true;
					continue;
				}
			}
			// Retail's supported conditions are constant, so select the branch once at load.
			FSWGIffChunk Branch;
			if (!Reader.FindChildForm(Pcmd, bTrue ? TopsTag : FopsTag, Branch)) continue;
			FSWGIffChunk Info;
			if (!Reader.FindChildChunk(Branch, SWGIffTags::Info, Info)) return false;
			FSWGIFFChunkReader InfoReader(Info, Reader);
			int32 Count;
			if (!InfoReader.ReadValueLE(Count) || Count < 0) return false;
			int32 Seen = 0;
			for (const FSWGIffChunk& OpChunk : Reader.ReadChildren(Branch))
			{
				if (!OpChunk.IsForm() && OpChunk.Tag == SWGIffTags::Info) continue;
				++Seen;
				if (OpChunk.IsForm()) { OutRenderer.bHasUnsupportedOps = true; continue; }
				if (OpChunk.Tag == NopTag)
				{
					if (OpChunk.DataSize != 0) return false;
					continue;
				}
				FSWGIFFChunkReader OpReader(OpChunk, Reader);
				FOp Op;
				if (OpChunk.Tag == StfpTag)
				{
					Op.Kind = EOpKind::SetFactorFromPalette;
					uint16 Shader;
					uint8 Private;
					if (!OpReader.ReadValueLE(Shader) || !OpReader.ReadValueLE(Op.Tag)
						|| !OpReader.ReadTerminiatedString(Op.Palette) || !OpReader.ReadTerminiatedString(Op.Variable)
						|| !OpReader.ReadValueLE(Private)) return false;
					Op.ShaderIndex = Shader;
					Op.Palette = NormalizePath(Op.Palette);
				}
				else if (OpChunk.Tag == SstcTag || OpChunk.Tag == Sst1Tag || OpChunk.Tag == Sst2Tag || OpChunk.Tag == StfTag || OpChunk.Tag == StfaTag)
				{
					if (!OpReader.ReadValueLE(Op.ShaderIndex) || !OpReader.ReadValueLE(Op.Tag)) return false;
					if (OpChunk.Tag == StfTag || OpChunk.Tag == StfaTag)
					{
						Op.Kind = OpChunk.Tag == StfTag ? EOpKind::SetFactorFromVariable : EOpKind::SetFactorAlpha;
						if (OpChunk.Tag == StfaTag)
						{
							uint8 R, G, B;
							if (!OpReader.ReadValueLE(R) || !OpReader.ReadValueLE(G) || !OpReader.ReadValueLE(B)) return false;
							Op.FactorRgb = (uint32(R) << 16) | (uint32(G) << 8) | B;
						}
						if (!OpReader.ReadTerminiatedString(Op.Variable)) return false;
					}
					else
					{
						Op.Kind = OpChunk.Tag == SstcTag ? EOpKind::SetTexture : (OpChunk.Tag == Sst1Tag ? EOpKind::SetTextureByVariable : EOpKind::SetTextureByVariables);
						if (!OpReader.ReadValueLE(Op.TextureBase)) return false;
						if (OpChunk.Tag != SstcTag && (!OpReader.ReadTerminiatedString(Op.Variable) || !OpReader.ReadValueLE(Op.TextureCount) || Op.TextureCount <= 0)) return false;
						if (OpChunk.Tag == Sst2Tag && (!OpReader.ReadTerminiatedString(Op.SecondVariable) || !OpReader.ReadValueLE(Op.SecondTextureCount) || Op.SecondTextureCount <= 0)) return false;
					}
				}
				else { OutRenderer.bHasUnsupportedOps = true; continue; }
				if (!OpReader.AtEnd() || !OutRenderer.Shaders.IsValidIndex(Op.ShaderIndex)) return false;
				OutRenderer.Ops.Add(MoveTemp(Op));
			}
			if (Seen != Count) return false;
		}
	}
	return !OutRenderer.Draws.IsEmpty() && (!OutRenderer.VertexBuffers.IsEmpty()
		|| OutRenderer.Draws.ContainsByPredicate([](const FDraw& Draw) { return Draw.bClearColor; }));
}

bool FSWGTextureRenderer::IsAlphaBlended(int32 ShaderIndex) const
{
	return Shaders.IsValidIndex(ShaderIndex) && Shaders[ShaderIndex].Contains(TEXT("alphablend"));
}

TArray<FString> FSWGTextureRenderer::GetVariables() const
{
	TArray<FString> Result;
	for (const FOp& Op : Ops)
	{
		if (!Op.Variable.IsEmpty())
		{
			Result.AddUnique(Op.Variable);
		}
		if (!Op.SecondVariable.IsEmpty()) Result.AddUnique(Op.SecondVariable);
	}
	return Result;
}

TArray<FSWGMeshTextureRendererRef> FSWGMeshTextureRendererReader::Read(const FSWGIffReader& MgnReader)
{
	TArray<FSWGMeshTextureRendererRef> Result;
	FSWGIffChunk Root, Trts;
	if (!MgnReader.IsValid() || !MgnReader.FindForm(SkmgTag, Root))
	{
		return Result;
	}
	const TArray<FSWGIffChunk> Versions = MgnReader.ReadChildren(Root);
	if (Versions.IsEmpty() || !MgnReader.FindChildForm(Versions[0], TrtsTag, Trts))
	{
		return Result;
	}
	for (const FSWGIffChunk& Entry : MgnReader.FindAllChildChunks(Trts, TrtTag))
	{
		FSWGIFFChunkReader EntryReader(Entry, MgnReader);
		FSWGMeshTextureRendererRef Ref;
		Ref.RendererPath = NormalizePath(EntryReader.ReadTerminiatedString());
		int32 Count = 0;
		EntryReader.ReadValueLE(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			int32 ShaderIndex = 0;
			uint32 Tag = 0;
			if (!EntryReader.ReadValueLE(ShaderIndex) || !EntryReader.ReadValueLE(Tag))
			{
				break;
			}
			Ref.Targets.Emplace(ShaderIndex, Tag);
		}
		Result.Add(MoveTemp(Ref));
	}
	return Result;
}

TArray<FString> FSWGMeshTextureRendererReader::ReadShaderNames(const FSWGIffReader& MgnReader)
{
	TArray<FString> Result;
	FSWGIffChunk Root;
	if (!MgnReader.IsValid() || !MgnReader.FindForm(SkmgTag, Root))
	{
		return Result;
	}
	const TArray<FSWGIffChunk> Versions = MgnReader.ReadChildren(Root);
	if (Versions.IsEmpty())
	{
		return Result;
	}
	for (const FSWGIffChunk& Child : MgnReader.FindChildForms(Versions[0]))
	{
		FSWGIffChunk Name;
		if (Child.FormType == PsdtTag && MgnReader.FindChildChunk(Child, SWGIffTags::Name, Name))
		{
			FSWGIFFChunkReader NameReader(Name, MgnReader);
			Result.Add(NormalizePath(NameReader.ReadTerminiatedString()));
		}
	}
	return Result;
}

int32 FSWGTextureCompositor::ResolveTextureIndex(const FSWGTextureRenderer::FOp& Op, int32 VariableValue, int32 SecondVariableValue)
{
	int64 Index = INDEX_NONE;
	switch (Op.Kind)
	{
		case FSWGTextureRenderer::EOpKind::SetTexture:
			return Op.TextureBase;
		case FSWGTextureRenderer::EOpKind::SetTextureByVariable:
			if (Op.TextureCount <= 0) return INDEX_NONE;
			Index = int64(Op.TextureBase) + FMath::Clamp(VariableValue, 0, Op.TextureCount - 1);
			break;
		case FSWGTextureRenderer::EOpKind::SetTextureByVariables:
			if (Op.TextureCount <= 0 || Op.SecondTextureCount <= 0) return INDEX_NONE;
			Index = int64(Op.TextureBase) + int64(FMath::Clamp(VariableValue, 0, Op.TextureCount - 1)) * Op.SecondTextureCount
				+ FMath::Clamp(SecondVariableValue, 0, Op.SecondTextureCount - 1);
			break;
		default:
			return INDEX_NONE;
	}
	return Index >= 0 && Index <= MAX_int32 ? int32(Index) : INDEX_NONE;
}

int32 FSWGTextureCompositor::ResolvePaletteIndex(int32 VariableValue, int32 PaletteSize)
{
	return (VariableValue < 0 || VariableValue >= PaletteSize) ? PaletteSize - 1 : VariableValue;
}

bool FSWGTextureCompositor::Compose(const FSWGTextureRenderer& Renderer, const FInputs& Inputs, FSWGImage& OutImage)
{
	OutImage = FSWGImage();
	if (Renderer.bHasUnsupportedOps || Renderer.Width <= 0 || Renderer.Height <= 0
		|| Renderer.Width > 8192 || Renderer.Height > 8192 || Renderer.Draws.IsEmpty())
	{
		return false;
	}

	// Per-shader state left by the ops.
	struct FShaderState
	{
		const FSWGImage* Texture = nullptr;
		FLinearColor Factor = FLinearColor::White;
	};
	TArray<FShaderState> States;
	States.SetNum(FMath::Max(Renderer.Shaders.Num(), 1));
	for (const FSWGTextureRenderer::FOp& Op : Renderer.Ops)
	{
		if (!States.IsValidIndex(Op.ShaderIndex))
		{
			continue;
		}
		const int32 Value = Op.Variable.IsEmpty() || !Inputs.GetVariable ? 0 : Inputs.GetVariable(Op.Variable);
		if (Op.Kind == FSWGTextureRenderer::EOpKind::SetFactorFromPalette)
		{
			const FSWGPalette* Palette = Inputs.LoadPalette ? Inputs.LoadPalette(Op.Palette) : nullptr;
			if (Palette && !Palette->Colors.IsEmpty())
			{
				const FColor Color = Palette->Colors[ResolvePaletteIndex(Value, Palette->Colors.Num())];
				States[Op.ShaderIndex].Factor = FLinearColor(Color.R / 255.f, Color.G / 255.f, Color.B / 255.f, 1.f);
			}
			continue;
		}
		if (Op.Kind == FSWGTextureRenderer::EOpKind::SetFactorFromVariable || Op.Kind == FSWGTextureRenderer::EOpKind::SetFactorAlpha)
		{
			const uint32 Packed = Op.Kind == FSWGTextureRenderer::EOpKind::SetFactorAlpha
				? (uint32(uint8(Value)) << 24) | Op.FactorRgb : uint32(Value);
			States[Op.ShaderIndex].Factor = FLinearColor(((Packed >> 16) & 255) / 255.f, ((Packed >> 8) & 255) / 255.f,
				(Packed & 255) / 255.f, (Packed >> 24) / 255.f);
			continue;
		}
		const int32 SecondValue = Op.SecondVariable.IsEmpty() || !Inputs.GetVariable ? 0 : Inputs.GetVariable(Op.SecondVariable);
		const int32 TextureIndex = ResolveTextureIndex(Op, Value, SecondValue);
		if (Renderer.Textures.IsValidIndex(TextureIndex) && Inputs.LoadImage)
		{
			States[Op.ShaderIndex].Texture = Inputs.LoadImage(Renderer.Textures[TextureIndex]);
		}
	}

	OutImage.Width = Renderer.Width;
	OutImage.Height = Renderer.Height;
	OutImage.Pixels.Init(FColor(0, 0, 0, 0), Renderer.Width * Renderer.Height);

	for (const FSWGTextureRenderer::FDraw& Draw : Renderer.Draws)
	{
		if (Draw.bClearColor)
		{
			const uint32 Color = Draw.ClearColor;
			OutImage.Pixels.Init(FColor((Color >> 16) & 255, (Color >> 8) & 255, Color & 255, Color >> 24), Renderer.Width * Renderer.Height);
			continue;
		}
		if (!Renderer.VertexBuffers.IsValidIndex(Draw.VertexBuffer) || !States.IsValidIndex(Draw.ShaderIndex))
		{
			continue;
		}
		const TArray<FSWGTextureRenderer::FVertex>& Fan = Renderer.VertexBuffers[Draw.VertexBuffer];
		const FShaderState& State = States[Draw.ShaderIndex];
		if (!State.Texture || !State.Texture->IsValid() || Fan.Num() < 3)
		{
			continue;
		}
		const bool bBlend = Renderer.IsAlphaBlended(Draw.ShaderIndex);

		// Pixel centres sit at (i + 0.5); the view is the unit square with y down.
		for (int32 PixelY = 0; PixelY < Renderer.Height; ++PixelY)
		{
			for (int32 PixelX = 0; PixelX < Renderer.Width; ++PixelX)
			{
				const float PointX = (PixelX + 0.5f) / Renderer.Width;
				const float PointY = (PixelY + 0.5f) / Renderer.Height;
				float U = 0.f, V = 0.f;
				bool bInside = false;
				for (int32 Triangle = 1; Triangle + 1 < Fan.Num() && !bInside; ++Triangle)
				{
					const FSWGTextureRenderer::FVertex& A = Fan[0];
					const FSWGTextureRenderer::FVertex& B = Fan[Triangle];
					const FSWGTextureRenderer::FVertex& C = Fan[Triangle + 1];
					const float Det = (B.Y - C.Y) * (A.X - C.X) + (C.X - B.X) * (A.Y - C.Y);
					if (FMath::IsNearlyZero(Det))
					{
						continue;
					}
					const float WeightA = ((B.Y - C.Y) * (PointX - C.X) + (C.X - B.X) * (PointY - C.Y)) / Det;
					const float WeightB = ((C.Y - A.Y) * (PointX - C.X) + (A.X - C.X) * (PointY - C.Y)) / Det;
					const float WeightC = 1.f - WeightA - WeightB;
					constexpr float Slack = -1e-5f;
					if (WeightA >= Slack && WeightB >= Slack && WeightC >= Slack)
					{
						U = WeightA * A.U + WeightB * B.U + WeightC * C.U;
						V = WeightA * A.V + WeightB * B.V + WeightC * C.V;
						bInside = true;
					}
				}
				if (!bInside)
				{
					continue;
				}

				const FLinearColor Texel = State.Texture->SampleBilinear(U, V);
				const FLinearColor Source(Texel.R * State.Factor.R, Texel.G * State.Factor.G, Texel.B * State.Factor.B, Texel.A);
				FColor& Destination = OutImage.Pixels[PixelY * Renderer.Width + PixelX];
				FLinearColor Result = Source;
				if (bBlend)
				{
					const FLinearColor Existing(Destination.R / 255.f, Destination.G / 255.f, Destination.B / 255.f, Destination.A / 255.f);
					const float Weight = Source.A;
					Result = FLinearColor(
						Source.R * Weight + Existing.R * (1.f - Weight),
						Source.G * Weight + Existing.G * (1.f - Weight),
						Source.B * Weight + Existing.B * (1.f - Weight),
						Source.A * Weight + Existing.A * (1.f - Weight));
				}
				Destination = FColor(
					(uint8)FMath::Clamp(FMath::RoundToInt(Result.R * 255.f), 0, 255),
					(uint8)FMath::Clamp(FMath::RoundToInt(Result.G * 255.f), 0, 255),
					(uint8)FMath::Clamp(FMath::RoundToInt(Result.B * 255.f), 0, 255),
					(uint8)FMath::Clamp(FMath::RoundToInt(Result.A * 255.f), 0, 255));
			}
		}
	}
	return true;
}

UTexture2D* FSWGImageTexture::Create(const FSWGImage& Image, const FName& BaseName, bool bSRGB)
{
	if (!Image.IsValid())
	{
		return nullptr;
	}
	const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), BaseName);
	// No image data here, so the only UpdateResource is the one after every mip is in place (see FSWGDDSTextureLoader).
	UTexture2D* Texture = UTexture2D::CreateTransient(Image.Width, Image.Height, PF_B8G8R8A8, UniqueName);
	if (!Texture)
	{
		return nullptr;
	}

	TArray<FColor> Level = Image.Pixels;
	int32 LevelWidth = Image.Width;
	int32 LevelHeight = Image.Height;
	bool bFirst = true;
	while (true)
	{
		FTexture2DMipMap* MipMap;
		if (bFirst)
		{
			MipMap = &Texture->GetPlatformData()->Mips[0];
		}
		else
		{
			MipMap = new FTexture2DMipMap(LevelWidth, LevelHeight, 1);
			Texture->GetPlatformData()->Mips.Add(MipMap);
		}
		MipMap->BulkData.Lock(LOCK_READ_WRITE);
		void* Dest = MipMap->BulkData.Realloc(Level.Num() * sizeof(FColor));
		FMemory::Memcpy(Dest, Level.GetData(), Level.Num() * sizeof(FColor));
		MipMap->BulkData.Unlock();
		bFirst = false;

		if (LevelWidth == 1 && LevelHeight == 1)
		{
			break;
		}
		const int32 NextWidth = FMath::Max(LevelWidth / 2, 1);
		const int32 NextHeight = FMath::Max(LevelHeight / 2, 1);
		TArray<FColor> Next;
		Next.SetNumUninitialized(NextWidth * NextHeight);
		for (int32 Y = 0; Y < NextHeight; ++Y)
		{
			for (int32 X = 0; X < NextWidth; ++X)
			{
				int32 Sum[4] = {};
				for (int32 DeltaY = 0; DeltaY < 2; ++DeltaY)
				{
					for (int32 DeltaX = 0; DeltaX < 2; ++DeltaX)
					{
						const FColor& Source = Level[FMath::Min(Y * 2 + DeltaY, LevelHeight - 1) * LevelWidth + FMath::Min(X * 2 + DeltaX, LevelWidth - 1)];
						Sum[0] += Source.R; Sum[1] += Source.G; Sum[2] += Source.B; Sum[3] += Source.A;
					}
				}
				Next[Y * NextWidth + X] = FColor((Sum[0] + 2) / 4, (Sum[1] + 2) / 4, (Sum[2] + 2) / 4, (Sum[3] + 2) / 4);
			}
		}
		Level = MoveTemp(Next);
		LevelWidth = NextWidth;
		LevelHeight = NextHeight;
	}

	Texture->SRGB = bSRGB;
	Texture->NeverStream = true;
	Texture->MipGenSettings = TMGS_LeaveExistingMips;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->UpdateResource();
	return Texture;
}
