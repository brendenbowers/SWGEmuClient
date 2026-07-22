#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/**
 * Bridges SWG's on-disk .dds ground textures (BC-compressed, extracted raw
 * from TRE archives via USWGTreSubsystem::ExtractFile) into runtime
 * UTexture2D assets. UE5.8 has no built-in DDS->UTexture2D path outside the
 * editor's import pipeline (confirmed via engine-wide source search) — this
 * hand-rolls the same steps CreateTransient + manual mip append already use
 * elsewhere in this plugin for the procedurally-baked heightmap texture
 * (SWGTerrainSubsystem::AddLandscapeComponent), just sourcing DXGI format and
 * per-mip compressed bytes from UE::DDS::FDDSFile instead of raw pixels.
 */
class SWGTRE_API FSWGDDSTextureLoader
{
public:
	/**
	 * Parses DDSBytes (a whole .dds file's raw bytes) and builds a transient
	 * UTexture2D with its full mip chain. Returns nullptr if the buffer isn't
	 * a recognized/supported DDS (bad header, or a DXGI format with no
	 * EPixelFormat mapping below — every format SWG actually ships, BC1/BC3/
	 * BC5/BC7 diffuse+normal maps, is supported).
	 */
	static UTexture2D* LoadTexture2D(const TArray<uint8>& DDSBytes, const FName& TextureName, bool bSRGB,
		bool bLegacyDXT5Normal = false);
};
