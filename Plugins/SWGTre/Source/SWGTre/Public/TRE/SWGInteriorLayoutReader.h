#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/**
 * One placement from an interior layout: client-only decoration (benches,
 * containers, debris) in a named room. No server identity — Core3 never reads
 * the file, so none of this is in the world snapshot or on the wire.
 */
struct FSWGInteriorLayoutNode
{
	/** Shared object template, e.g. "object/tangible/furniture/cheap/shared_couch_s01.iff". */
	FString TemplatePath;

	/** Matches FSWGPobCell::CellName, not the cell index. */
	FString CellName;

	/** Cell-relative, UE space — same treatment as FSWGPobPortalRef::DoorHardpoint. */
	FTransform Transform;
};

struct FSWGInteriorLayoutData
{
	TArray<FSWGInteriorLayoutNode> Nodes;
};

/**
 * Parses interiorlayout/<building>.ilf (a building template's SBOT
 * interiorLayoutFileName): FORM INLY > 0000 > NODE*, each NODE being
 * [template\0][cellName\0][3x4 transform] as FSWGIFFChunkReader::ReadTransform reads it.
 */
class SWGTRE_API FSWGInteriorLayoutReader
{
public:
	static bool ReadInteriorLayout(const FSWGIffReader& Reader, FSWGInteriorLayoutData& OutData);
};
