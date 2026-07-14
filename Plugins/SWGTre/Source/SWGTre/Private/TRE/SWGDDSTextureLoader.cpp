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
}

UTexture2D* FSWGDDSTextureLoader::LoadTexture2D(const TArray<uint8>& DDSBytes, const FName& TextureName, bool bSRGB)
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

	const EPixelFormat PixelFormat = MapDXGIFormatToPixelFormat(Dds->DXGIFormat);
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

	UTexture2D* Texture = UTexture2D::CreateTransient(Mip0.Width, Mip0.Height, PixelFormat, TextureName,
		TConstArrayView64<uint8>(Mip0.Data, Mip0.DataSize));

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

		FTexture2DMipMap* MipMap = new FTexture2DMipMap(Mip.Width, Mip.Height, 1);
		Texture->GetPlatformData()->Mips.Add(MipMap);
		MipMap->BulkData.Lock(LOCK_READ_WRITE);
		void* DestData = MipMap->BulkData.Realloc(Mip.DataSize);
		FMemory::Memcpy(DestData, Mip.Data, Mip.DataSize);
		MipMap->BulkData.Unlock();
	}

	Texture->SRGB = bSRGB;
	Texture->CompressionSettings = TC_Default;

	// Without these, the engine's streaming/mip-regeneration machinery can
	// silently replace this manually-populated PlatformData with a blank
	// default (since it expects mips derived from Source + the DDC, which this
	// texture never has). NeverStream pins mips resident from PlatformData;
	// LeaveExistingMips stops regeneration from an unpopulated Source.
	Texture->NeverStream = true;
	Texture->MipGenSettings = TMGS_LeaveExistingMips;

	Texture->UpdateResource();

	delete Dds;
	return Texture;
}
