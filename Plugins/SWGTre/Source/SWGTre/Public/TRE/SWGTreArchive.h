#pragma once

#include "CoreMinimal.h"

/** One indexed entry in a .tre archive — a virtual path plus where/how to extract its bytes. */
struct FSWGTreRecord
{
	FString Name;          // virtual path, e.g. "object/mobile/shared_bantha.iff"
	uint32  FileOffset = 0;
	uint32  UncompSize = 0;
	uint32  CompSize   = 0;
	uint32  CompType   = 0; // 0 = stored, 2 = zlib
};

/**
 * Reads one SWG .tre archive: header ('TREE'/'0005', little-endian on disk),
 * a name-and-offset record table (optionally zlib-compressed as a block),
 * and a null-terminated name blob (also optionally zlib-compressed).
 * Ported from the exploratory TreTool2.ps1/TreReader.
 *
 * Multiple .tre files can (and do) contain the same virtual path — later
 * patch archives override earlier ones. This class only indexes a single
 * file; USWGTreSubsystem layers several of these with override precedence.
 */
class SWGTRE_API FSWGTreArchive
{
public:
	bool LoadIndex(const FString& InFilePath);

	const FString& GetFilePath() const { return FilePath; }
	const TArray<FSWGTreRecord>& GetRecords() const { return Records; }

	const FSWGTreRecord* FindRecord(const FString& VirtualPath) const;

	/** Reads and (if needed) decompresses one record's bytes from disk. */
	TArray<uint8> Extract(const FSWGTreRecord& Record) const;

private:
	FString FilePath;
	TArray<FSWGTreRecord> Records;
	TMap<FString, int32> NameToIndex;

	static uint32 ReadUInt32LE(const uint8* Bytes);
};
