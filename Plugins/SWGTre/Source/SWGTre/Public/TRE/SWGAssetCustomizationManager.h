#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/** One customization variable as it applies to a specific appearance file — either a plain bounded integer (a blend/morph slider) or a palette-color index into a named .pal file. */
struct FSWGCustomizationVariableDef
{
	FString Name;
	bool bIsPalette = false;
	int32 DefaultValue = 0;

	// Valid when !bIsPalette.
	int32 MinValue = 0;
	int32 MaxValue = 0;

	// Valid when bIsPalette.
	FString PaletteFileName;
};

/**
 * Reads "customization/asset_customization_manager.iff" and resolves, for a
 * given appearance file, which customization variables apply to it and what
 * each one does. Mirrors Core3's AssetCustomizationManagerTemplate
 * (MMOCoreORB/src/templates/customization/AssetCustomizationManagerTemplate.cpp)
 * byte-for-byte — see that file's readObject/getCustomizationVariablesFromMap
 * for the reference implementation this was ported from. Wire layout:
 * FORM ACST > FORM 0000 > chunks NAME, PNOF, VNOF, DEFV, IRNG, RTYP, UCMP,
 * ULST, UIDX, LLST, LIDX, CIDX (in that order), all little-endian, no
 * padding between records:
 *
 *   NAME: raw bytes — back-to-back null-terminated strings, addressed by
 *         absolute byte offset from PNOF/VNOF entries.
 *   PNOF, VNOF: uint16[] — each a NAME byte offset (palette filenames /
 *         variable names respectively).
 *   DEFV: int32[] — default values, 1-based index (UCMP's DefvIndex byte).
 *   IRNG: {int32 Min, int32 Max}[] — ranged-int bounds, 1-based index.
 *   RTYP: uint16[] — 1-based index (UCMP's RtypIndex byte); high bit 0x8000
 *         set means "palette" and the low 15 bits index PNOF, clear means
 *         "ranged int" and the low 15 bits index IRNG.
 *   UCMP: {uint8 VnofIndex, uint8 RtypIndex, uint8 DefvIndex}[] (all
 *         1-based) — one full variable descriptor, indexed by ULST.
 *   ULST: uint16[] — 1-based UCMP indices, sliced by UIDX.
 *   UIDX: {uint16 Key, uint16 UlstStartIndex(0-based), uint8 Count}[] —
 *         appearance-key -> its own (non-inherited) variable list.
 *   LLST: uint16[] — nested appearance keys, sliced by LIDX.
 *   LIDX: {uint16 Key, uint16 LlstStartIndex(0-based), uint8 Count}[] —
 *         appearance-key -> keys it inherits variables from (recursively;
 *         an inherited variable never overrides one the key already has via
 *         its own UIDX entry).
 *   CIDX: {uint32 AppearanceFileCrc, uint16 Key}[], sorted ascending by
 *         Crc — resolves an appearance file (hashed via FSWGCrc32, matching
 *         Core3's String::hashCode()) to its UIDX/LIDX key.
 */
class SWGTRE_API FSWGAssetCustomizationManager
{
public:
	bool Read(const FSWGIffReader& Reader);

	/**
	 * AppearanceFilePath is hashed via FSWGCrc32::HashString and looked up in
	 * CIDX — same string Core3's server hashes for the same purpose
	 * (templateData->getAppearanceFilename() at the various
	 * AssetCustomizationManagerTemplate::getCustomizationVariables call
	 * sites). OutVariables is additive (existing entries aren't cleared) so
	 * callers can merge multiple appearance files' variables in one map.
	 * bSkipSharedOwner matches Core3's own flag — true skips any variable
	 * whose name contains "/shared_owner/" (used server-side to exclude
	 * owner-configurable variables from some contexts; false includes them).
	 */
	void GetCustomizationVariables(const FString& AppearanceFilePath, bool bSkipSharedOwner, TMap<FString, FSWGCustomizationVariableDef>& OutVariables) const;

private:
	// These are memcpy'd straight from file bytes (see ReadFixedRecords in
	// the .cpp) — pack(1) is required so sizeof() matches the on-disk record
	// size exactly; default alignment would pad FIdxEntry to 6 bytes and
	// FCidxEntry to 8, silently misreading every record after the first.
#pragma pack(push, 1)
	struct FUcmpEntry { uint8 VnofIndex = 0; uint8 RtypIndex = 0; uint8 DefvIndex = 0; };
	struct FIdxEntry { uint16 Key = 0; uint16 StartIndex = 0; uint8 Count = 0; };
	struct FCidxEntry { uint32 Crc = 0; uint16 Key = 0; };
#pragma pack(pop)

	FString GetNameString(uint16 ByteOffset) const;
	uint16 SearchCidx(uint32 Crc) const;
	const FIdxEntry* SearchUidx(uint16 Key) const;
	const FIdxEntry* SearchLidx(uint16 Key) const;
	void GetVariablesForKey(uint16 Key, bool bSkipSharedOwner, TMap<FString, FSWGCustomizationVariableDef>& OutResult) const;

	TArray<uint8> NameTable;
	TArray<uint16> PnofTable;
	TArray<uint16> VnofTable;
	TArray<int32> DefvTable;
	TArray<TPair<int32, int32>> IrngTable;
	TArray<uint16> RtypTable;
	TArray<FUcmpEntry> UcmpTable;
	TArray<uint16> UlstTable;
	TArray<FIdxEntry> UidxTable;
	TArray<uint16> LlstTable;
	TArray<FIdxEntry> LidxTable;
	TArray<FCidxEntry> CidxTable;
};
