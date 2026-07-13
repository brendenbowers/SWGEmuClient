#pragma once

#include "CoreMinimal.h"

/**
 * One node in an IFF chunk tree: either a container ("FORM" + a 4-char
 * FormType, e.g. FORM CSTB) or a leaf data chunk (any other 4-char tag, e.g.
 * DATA/CRCT/STRT/STNG). Sizes are big-endian on disk — this is a distinct
 * format from FSWGPacket's little-endian SOE wire format, despite both being
 * "chunked binary."
 *
 * DataOffset/DataSize describe the payload region within the reader's owned
 * buffer: for FORM chunks that's the 4-byte FormType followed by child
 * chunks (DataOffset points past the FormType, at the first child); for leaf
 * chunks it's the raw chunk bytes.
 */
struct FSWGIffChunk
{
	FString Tag;      // "FORM" or a 4-char leaf tag (DATA, CRCT, STRT, STNG, PCNT, XXXX, ...)
	FString FormType; // only set when Tag == "FORM" (e.g. CSTB, 0000, SCOT, DERV)
	int32 DataOffset = 0;
	int32 DataSize = 0;

	bool IsForm() const { return Tag == TEXT("FORM"); }
};

/**
 * Generic reader for SWG's IFF-style container format (FORM/tag + big-endian
 * size + data, arbitrarily nested) — used by the CRC string tables, object
 * templates (SCOT/STOT/SHOT), and most other .iff assets in the TRE archives.
 * Ported from the exploratory TreTool2.ps1/DumpIffNode logic.
 */
class SWGTRE_API FSWGIffReader
{
public:
	explicit FSWGIffReader(TArray<uint8> InData);

	bool IsValid() const { return Data.Num() > 0; }

	/** Top-level chunks in the buffer (an .iff file is usually exactly one FORM). */
	TArray<FSWGIffChunk> ReadChunks() const;

	/** Direct children of a FORM chunk. Empty if Chunk isn't a FORM. */
	TArray<FSWGIffChunk> ReadChildren(const FSWGIffChunk& FormChunk) const;

	/** Depth-first search for the first FORM anywhere in the tree with the given FormType. */
	bool FindForm(const FString& FormType, FSWGIffChunk& OutChunk) const;

	/**
	 * The outer FORM type of the file (e.g. "SCOT", "STAT", "SBOT") — the
	 * actual class discriminator for object templates, more reliable than the
	 * CRC path prefix. Look this up in the FormTag->ActorClass DataTable
	 * (FSWGFormTagMapping) to resolve which actor to spawn. Returns NAME_None
	 * if the buffer's first chunk isn't a FORM at all.
	 */
	FName GetRootFormType() const;

	/** Raw byte pointer to a chunk's payload (valid for the reader's lifetime). */
	const uint8* GetChunkData(const FSWGIffChunk& Chunk) const;
	int32 GetChunkSize(const FSWGIffChunk& Chunk) const { return Chunk.DataSize; }

private:
	TArray<FSWGIffChunk> ReadChunksInRange(int32 Pos, int32 End) const;
	bool FindFormRecursive(int32 Pos, int32 End, const FString& FormType, FSWGIffChunk& OutChunk) const;

	static int32 ReadInt32BE(const uint8* Bytes);

	TArray<uint8> Data;
};
