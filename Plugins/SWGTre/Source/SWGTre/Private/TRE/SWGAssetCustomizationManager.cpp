#include "TRE/SWGAssetCustomizationManager.h"
#include "TRE/SWGIffTags.h"
#include "TRE/SWGCrc32.h"

namespace
{
	// Copies Chunk's raw payload into an array of fixed-size records,
	// truncating any trailing partial record (shouldn't happen in
	// well-formed data, but avoids reading past the end either way).
	template<typename T>
	TArray<T> ReadFixedRecords(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk)
	{
		TArray<T> Result;
		const int32 Count = Chunk.DataSize / sizeof(T);
		Result.SetNumUninitialized(Count);
		if (Count > 0)
		{
			FMemory::Memcpy(Result.GetData(), Reader.GetChunkData(Chunk), Count * sizeof(T));
		}
		return Result;
	}
}

bool FSWGAssetCustomizationManager::Read(const FSWGIffReader& Reader)
{
	if (!Reader.IsValid())
	{
		return false;
	}

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0 || !TopLevel[0].IsForm() || TopLevel[0].FormType != SWG_IFF_TAG('A','C','S','T'))
	{
		return false;
	}

	FSWGIffChunk Form0000;
	if (!Reader.FindChildForm(TopLevel[0], SWG_IFF_TAG('0','0','0','0'), Form0000))
	{
		return false;
	}

	FSWGIffChunk NameChunk, PnofChunk, VnofChunk, DefvChunk, IrngChunk, RtypChunk, UcmpChunk, UlstChunk, UidxChunk, LlstChunk, LidxChunk, CidxChunk;
	if (!Reader.FindChildChunk(Form0000, SWG_IFF_TAG('N','A','M','E'), NameChunk)) return false;
	if (!Reader.FindChildChunk(Form0000, SWG_IFF_TAG('P','N','O','F'), PnofChunk)) return false;
	if (!Reader.FindChildChunk(Form0000, SWG_IFF_TAG('V','N','O','F'), VnofChunk)) return false;
	if (!Reader.FindChildChunk(Form0000, SWG_IFF_TAG('D','E','F','V'), DefvChunk)) return false;
	if (!Reader.FindChildChunk(Form0000, SWG_IFF_TAG('I','R','N','G'), IrngChunk)) return false;
	if (!Reader.FindChildChunk(Form0000, SWG_IFF_TAG('R','T','Y','P'), RtypChunk)) return false;
	if (!Reader.FindChildChunk(Form0000, SWG_IFF_TAG('U','C','M','P'), UcmpChunk)) return false;
	if (!Reader.FindChildChunk(Form0000, SWG_IFF_TAG('U','L','S','T'), UlstChunk)) return false;
	if (!Reader.FindChildChunk(Form0000, SWG_IFF_TAG('U','I','D','X'), UidxChunk)) return false;
	if (!Reader.FindChildChunk(Form0000, SWG_IFF_TAG('L','L','S','T'), LlstChunk)) return false;
	if (!Reader.FindChildChunk(Form0000, SWG_IFF_TAG('L','I','D','X'), LidxChunk)) return false;
	if (!Reader.FindChildChunk(Form0000, SWG_IFF_TAG('C','I','D','X'), CidxChunk)) return false;

	NameTable.SetNumUninitialized(NameChunk.DataSize);
	if (NameChunk.DataSize > 0)
	{
		FMemory::Memcpy(NameTable.GetData(), Reader.GetChunkData(NameChunk), NameChunk.DataSize);
	}

	PnofTable = ReadFixedRecords<uint16>(Reader, PnofChunk);
	VnofTable = ReadFixedRecords<uint16>(Reader, VnofChunk);
	DefvTable = ReadFixedRecords<int32>(Reader, DefvChunk);
	IrngTable = ReadFixedRecords<TPair<int32, int32>>(Reader, IrngChunk);
	RtypTable = ReadFixedRecords<uint16>(Reader, RtypChunk);
	UcmpTable = ReadFixedRecords<FUcmpEntry>(Reader, UcmpChunk);
	UlstTable = ReadFixedRecords<uint16>(Reader, UlstChunk);
	UidxTable = ReadFixedRecords<FIdxEntry>(Reader, UidxChunk);
	LlstTable = ReadFixedRecords<uint16>(Reader, LlstChunk);
	LidxTable = ReadFixedRecords<FIdxEntry>(Reader, LidxChunk);
	CidxTable = ReadFixedRecords<FCidxEntry>(Reader, CidxChunk);

	return CidxTable.Num() > 0;
}

FString FSWGAssetCustomizationManager::GetNameString(uint16 ByteOffset) const
{
	if (!NameTable.IsValidIndex(ByteOffset))
	{
		return FString();
	}
	// Back-to-back null-terminated ANSI strings in one raw blob — safe to
	// treat NameTable.GetData()+ByteOffset as a C string since every string
	// in the table is itself null-terminated (including the last one, or
	// this would read past the array — matches Core3 treating this table as
	// a plain char*).
	return FString(ANSI_TO_TCHAR((const ANSICHAR*)NameTable.GetData() + ByteOffset));
}

uint16 FSWGAssetCustomizationManager::SearchCidx(uint32 Crc) const
{
	// Linear scan: correctness over Core3's bsearch micro-optimization —
	// CIDX only needs to be resolved a handful of times per session (this
	// project resolves it once per distinct appearance file, then GetOrBuild*
	// caches the result like everything else in the mesh pipeline).
	for (const FCidxEntry& Entry : CidxTable)
	{
		if (Entry.Crc == Crc)
		{
			return Entry.Key;
		}
	}
	return 0;
}

const FSWGAssetCustomizationManager::FIdxEntry* FSWGAssetCustomizationManager::SearchUidx(uint16 Key) const
{
	for (const FIdxEntry& Entry : UidxTable)
	{
		if (Entry.Key == Key)
		{
			return &Entry;
		}
	}
	return nullptr;
}

const FSWGAssetCustomizationManager::FIdxEntry* FSWGAssetCustomizationManager::SearchLidx(uint16 Key) const
{
	for (const FIdxEntry& Entry : LidxTable)
	{
		if (Entry.Key == Key)
		{
			return &Entry;
		}
	}
	return nullptr;
}

void FSWGAssetCustomizationManager::GetVariablesForKey(uint16 Key, bool bSkipSharedOwner, TMap<FString, FSWGCustomizationVariableDef>& OutResult) const
{
	if (const FIdxEntry* Uidx = SearchUidx(Key))
	{
		const uint16 FinalIndex = Uidx->StartIndex + Uidx->Count;
		for (uint16 i = Uidx->StartIndex; i < FinalIndex && UlstTable.IsValidIndex(i); ++i)
		{
			const uint16 UcmpIndex = UlstTable[i]; // 1-based
			if (!UcmpTable.IsValidIndex(UcmpIndex - 1))
			{
				continue;
			}
			const FUcmpEntry& Ucmp = UcmpTable[UcmpIndex - 1];

			if (!VnofTable.IsValidIndex(Ucmp.VnofIndex - 1))
			{
				continue;
			}
			const FString VariableName = GetNameString(VnofTable[Ucmp.VnofIndex - 1]);

			if (OutResult.Contains(VariableName))
			{
				continue; // A more specific (non-inherited) entry already won.
			}
			if (bSkipSharedOwner && VariableName.Contains(TEXT("/shared_owner/")))
			{
				continue;
			}
			if (!RtypTable.IsValidIndex(Ucmp.RtypIndex - 1))
			{
				continue;
			}

			FSWGCustomizationVariableDef Def;
			Def.Name = VariableName;
			Def.DefaultValue = DefvTable.IsValidIndex(Ucmp.DefvIndex - 1) ? DefvTable[Ucmp.DefvIndex - 1] : 0;

			const uint16 RtypValue = RtypTable[Ucmp.RtypIndex - 1];
			Def.bIsPalette = (RtypValue & 0x8000) != 0;
			const uint16 ResultIndex = RtypValue & 0x7FFF;

			if (Def.bIsPalette)
			{
				Def.PaletteFileName = PnofTable.IsValidIndex(ResultIndex - 1) ? GetNameString(PnofTable[ResultIndex - 1]) : FString();
			}
			else if (IrngTable.IsValidIndex(ResultIndex - 1))
			{
				Def.MinValue = IrngTable[ResultIndex - 1].Key;
				Def.MaxValue = IrngTable[ResultIndex - 1].Value;
			}

			OutResult.Add(VariableName, MoveTemp(Def));
		}
	}

	// Inherited variables (via LIDX/LLST) never override a key's own
	// (already-added, above) entries — the Contains() check in the loop
	// above enforces that for recursive calls too, since OutResult is
	// shared/accumulated across the whole recursion.
	if (const FIdxEntry* Lidx = SearchLidx(Key))
	{
		const uint16 FinalIndex = Lidx->StartIndex + Lidx->Count;
		for (uint16 i = Lidx->StartIndex; i < FinalIndex && LlstTable.IsValidIndex(i); ++i)
		{
			GetVariablesForKey(LlstTable[i], bSkipSharedOwner, OutResult);
		}
	}
}

void FSWGAssetCustomizationManager::GetCustomizationVariables(const FString& AppearanceFilePath, bool bSkipSharedOwner, TMap<FString, FSWGCustomizationVariableDef>& OutVariables) const
{
	const uint32 Crc = FSWGCrc32::HashString(AppearanceFilePath);
	const uint16 Key = SearchCidx(Crc);
	if (Key == 0)
	{
		return;
	}
	GetVariablesForKey(Key, bSkipSharedOwner, OutVariables);
}
