#include "TRE/SWGDDSTextureLoader.h"
#include "DDSFile.h"
#include "Engine/Texture2D.h"

namespace
{
	// Conservative floor for "GetMax2DTextureDimension()" (the RHI module isn't
	// a dependency of this plugin, and every D3D11/12-class GPU guarantees at
	// least this much) — confirmed as the real ceiling to check against via a
	// live crash: "TextureDesc.Extent.X <= (int32)GetMax2DTextureDimension()"
	// firing on the render thread for a malformed/misread .dds whose mip
	// dimensions came out nonsensically huge (garbage, not a real texture
	// size — every legitimate SWG texture is far smaller than this).
	constexpr int32 MaxTextureDimension = 16384;

	// Only the formats SWG's own ground/prop textures actually ship in need a
	// mapping — BC1/BC3 (diffuse, with/without alpha) and BC5 (normal maps) are
	// what's used in practice; BC7/uncompressed are handled too since they're
	// cheap to support and plausible for a re-exported/patched texture.
	EPixelFormat MapDXGIFormatToPixelFormat(UE::DDS::EDXGIFormat DXGIFormat)
	{
		using namespace UE::DDS;
		switch (DXGIFormat)
		{
		case EDXGIFormat::BC1_UNORM:
		case EDXGIFormat::BC1_UNORM_SRGB:
			return PF_DXT1;
		case EDXGIFormat::BC2_UNORM:
		case EDXGIFormat::BC2_UNORM_SRGB:
			return PF_DXT3;
		case EDXGIFormat::BC3_UNORM:
		case EDXGIFormat::BC3_UNORM_SRGB:
			return PF_DXT5;
		case EDXGIFormat::BC4_UNORM:
		case EDXGIFormat::BC4_SNORM:
			return PF_BC4;
		case EDXGIFormat::BC5_UNORM:
		case EDXGIFormat::BC5_SNORM:
			return PF_BC5;
		case EDXGIFormat::BC6H_UF16:
		case EDXGIFormat::BC6H_SF16:
			return PF_BC6H;
		case EDXGIFormat::BC7_UNORM:
		case EDXGIFormat::BC7_UNORM_SRGB:
			return PF_BC7;
		case EDXGIFormat::R8G8B8A8_UNORM:
		case EDXGIFormat::R8G8B8A8_UNORM_SRGB:
			return PF_R8G8B8A8;
		case EDXGIFormat::B8G8R8A8_UNORM:
		case EDXGIFormat::B8G8R8A8_UNORM_SRGB:
		case EDXGIFormat::B8G8R8X8_UNORM:
			return PF_B8G8R8A8;
		default:
			return PF_Unknown;
		}
	}

	// SWG's CNRM textures use the DXT5nm convention: tangent X is in alpha,
	// tangent Y is in green, and RGB is not a usable XYZ normal. Expand it to
	// a conventional BGRA tangent-space normal before handing it to a material.
	void DecodeLegacyDXT5NormalMip(const UE::DDS::FDDSMip& Mip, TArray<uint8>& OutBGRA)
	{
		const int32 Width = (int32)Mip.Width;
		const int32 Height = (int32)Mip.Height;
		OutBGRA.SetNumUninitialized((int64)Width * Height * 4);
		const uint8* Block = Mip.Data;
		for (int32 BlockY = 0; BlockY < (Height + 3) / 4; ++BlockY)
		{
			for (int32 BlockX = 0; BlockX < (Width + 3) / 4; ++BlockX, Block += 16)
			{
				uint8 Alpha[8] = { Block[0], Block[1] };
				if (Alpha[0] > Alpha[1])
				{
					for (int32 i = 1; i <= 6; ++i) Alpha[i + 1] = (uint8)(((7 - i) * Alpha[0] + i * Alpha[1]) / 7);
				}
				else
				{
					for (int32 i = 1; i <= 4; ++i) Alpha[i + 1] = (uint8)(((5 - i) * Alpha[0] + i * Alpha[1]) / 5);
					Alpha[6] = 0; Alpha[7] = 255;
				}
				uint64 AlphaBits = 0;
				for (int32 i = 0; i < 6; ++i) AlphaBits |= (uint64)Block[2 + i] << (8 * i);

				const uint16 Color0 = (uint16)Block[8] | ((uint16)Block[9] << 8);
				const uint16 Color1 = (uint16)Block[10] | ((uint16)Block[11] << 8);
				auto ExpandGreen = [](uint16 Color) { return (uint8)((((Color >> 5) & 63) * 255 + 31) / 63); };
				uint8 Green[4] = { ExpandGreen(Color0), ExpandGreen(Color1), 0, 0 };
				if (Color0 > Color1)
				{
					Green[2] = (uint8)((2 * Green[0] + Green[1]) / 3);
					Green[3] = (uint8)((Green[0] + 2 * Green[1]) / 3);
				}
				else
				{
					Green[2] = (uint8)((Green[0] + Green[1]) / 2);
					Green[3] = 0;
				}
				const uint32 ColorBits = (uint32)Block[12] | ((uint32)Block[13] << 8)
					| ((uint32)Block[14] << 16) | ((uint32)Block[15] << 24);

				for (int32 Pixel = 0; Pixel < 16; ++Pixel)
				{
					const int32 XPos = BlockX * 4 + Pixel % 4;
					const int32 YPos = BlockY * 4 + Pixel / 4;
					if (XPos >= Width || YPos >= Height) continue;
					const uint8 XByte = Alpha[(AlphaBits >> (3 * Pixel)) & 7];
					const uint8 YByte = Green[(ColorBits >> (2 * Pixel)) & 3];
					const float NX = XByte / 127.5f - 1.0f;
					const float NY = YByte / 127.5f - 1.0f;
					const uint8 ZByte = (uint8)FMath::Clamp(FMath::RoundToInt(
						(FMath::Sqrt(FMath::Max(0.0f, 1.0f - NX * NX - NY * NY)) * 0.5f + 0.5f) * 255.0f), 0, 255);
					uint8* Dest = OutBGRA.GetData() + ((int64)YPos * Width + XPos) * 4;
					Dest[0] = ZByte; Dest[1] = YByte; Dest[2] = XByte; Dest[3] = 255;
				}
			}
		}
	}
}

UTexture2D* FSWGDDSTextureLoader::LoadTexture2D(const TArray<uint8>& DDSBytes, const FName& TextureName, bool bSRGB,
	bool bLegacyDXT5Normal)
{
	using namespace UE::DDS;

	if (DDSBytes.Num() == 0)
	{
		return nullptr;
	}

	EDDSError Error = EDDSError::OK;
	FDDSFile* Dds = FDDSFile::CreateFromDDSInMemory(DDSBytes.GetData(), DDSBytes.Num(), &Error, EDDSReadMipMode::Full);
	if (!Dds)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGDDSTextureLoader: failed to parse DDS '%s' (error %d)"), *TextureName.ToString(), (int32)Error);
		return nullptr;
	}

	const bool bDecodeLegacyNormal = bLegacyDXT5Normal
		&& (Dds->DXGIFormat == EDXGIFormat::BC3_UNORM || Dds->DXGIFormat == EDXGIFormat::BC3_UNORM_SRGB);
	const EPixelFormat PixelFormat = bDecodeLegacyNormal ? PF_B8G8R8A8 : MapDXGIFormatToPixelFormat(Dds->DXGIFormat);
	if (PixelFormat == PF_Unknown || !Dds->IsValidTexture2D() || Dds->Mips.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGDDSTextureLoader: unsupported DDS '%s' (DXGIFormat=%s, mips=%d)"),
			*TextureName.ToString(), DXGIFormatGetName(Dds->DXGIFormat), Dds->Mips.Num());
		delete Dds;
		return nullptr;
	}

	const FDDSMip& Mip0 = Dds->Mips[0];
	if (Mip0.Width == 0 || Mip0.Height == 0 || Mip0.DataSize <= 0
		|| Mip0.Width > MaxTextureDimension || Mip0.Height > MaxTextureDimension)
	{
		// A zero-sized or oversized mip 0 doesn't fail here — CreateTransient
		// accepts a bad size — but crashes the render thread later with a
		// D3D12RHI assertion once the RHI resource actually gets created.
		UE_LOG(LogTemp, Warning, TEXT("FSWGDDSTextureLoader: '%s' has an invalid mip 0 (%dx%d, %lld bytes) — refusing to load"),
			*TextureName.ToString(), Mip0.Width, Mip0.Height, Mip0.DataSize);
		delete Dds;
		return nullptr;
	}

	TArray<uint8> DecodedMip;
	if (bDecodeLegacyNormal) DecodeLegacyDXT5NormalMip(Mip0, DecodedMip);
	const uint8* Mip0Data = bDecodeLegacyNormal ? DecodedMip.GetData() : Mip0.Data;
	const int64 Mip0DataSize = bDecodeLegacyNormal ? DecodedMip.Num() : Mip0.DataSize;
	UTexture2D* Texture = UTexture2D::CreateTransient(Mip0.Width, Mip0.Height, PixelFormat, TextureName,
		TConstArrayView64<uint8>(Mip0Data, Mip0DataSize));

	if (!Texture)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGDDSTextureLoader: CreateTransient failed for '%s'"), *TextureName.ToString());
		delete Dds;
		return nullptr;
	}

	// Additional mips appended the same way SWGTerrainSubsystem's baked
	// heightmap texture does it — new FTexture2DMipMap + BulkData lock/
	// realloc/unlock, none of it editor-gated.
	for (int32 MipIndex = 1; MipIndex < Dds->Mips.Num(); ++MipIndex)
	{
		const FDDSMip& Mip = Dds->Mips[MipIndex];
		if (Mip.DataSize <= 0 || Mip.Width == 0 || Mip.Height == 0
			|| Mip.Width > MaxTextureDimension || Mip.Height > MaxTextureDimension)
		{
			// Stop the chain here rather than appending an invalid mip — same
			// crashes this whole function now guards against for mip 0 (see
			// above), just further down the chain.
			break;
		}

		if (bDecodeLegacyNormal) DecodeLegacyDXT5NormalMip(Mip, DecodedMip);
		const void* SourceData = bDecodeLegacyNormal ? DecodedMip.GetData() : Mip.Data;
		const int64 SourceDataSize = bDecodeLegacyNormal ? DecodedMip.Num() : Mip.DataSize;
		FTexture2DMipMap* MipMap = new FTexture2DMipMap(Mip.Width, Mip.Height, 1);
		Texture->GetPlatformData()->Mips.Add(MipMap);
		MipMap->BulkData.Lock(LOCK_READ_WRITE);
		void* DestData = MipMap->BulkData.Realloc(SourceDataSize);
		FMemory::Memcpy(DestData, SourceData, SourceDataSize);
		MipMap->BulkData.Unlock();
	}

	Texture->SRGB = bSRGB;
	Texture->CompressionSettings = bLegacyDXT5Normal ? TC_Normalmap : TC_Default;

	// Without these, the engine's streaming/mip-regeneration machinery can
	// silently replace this manually-populated PlatformData with a blank
	// default (since it expects mips derived from Source + the DDC, which this
	// texture never has). NeverStream pins mips resident from PlatformData;
	// LeaveExistingMips stops regeneration from an unpopulated Source.
	Texture->NeverStream = true;
	Texture->MipGenSettings = TMGS_LeaveExistingMips;
	// SWG shader textures are generally authored with wrapping address modes;
	// this is essential for world-space terrain UVs. Set it before the single
	// resource initialization below (reinitializing a transient texture after
	// it is bound can race the render thread).
	Texture->AddressX = TA_Wrap;
	Texture->AddressY = TA_Wrap;

	Texture->UpdateResource();

	delete Dds;
	return Texture;
}
