#pragma once

#include "CoreMinimal.h"

/**
 * A 4-character IFF tag (e.g. "FORM", "DATA", "SLOD"), packed into a uint32
 * so tag comparisons are a single integer compare instead of an FString
 * compare, and tag literals build with SWG_IFF_TAG(...) at compile time
 * instead of TEXT("...").
 */
struct FSWGIffTag
{
	uint32 Value = 0;

	constexpr FSWGIffTag() = default;
	constexpr explicit FSWGIffTag(uint32 InValue) : Value(InValue) {}

	friend constexpr bool operator==(FSWGIffTag A, FSWGIffTag B) { return A.Value == B.Value; }
	friend constexpr bool operator!=(FSWGIffTag A, FSWGIffTag B) { return A.Value != B.Value; }
	friend uint32 GetTypeHash(FSWGIffTag Tag) { return Tag.Value; }

	bool IsNone() const { return Value == 0; }

	/** First character of the tag — used for prefix checks like "starts with 'B'" (boundary/filter form tags). */
	constexpr TCHAR FirstChar() const { return (TCHAR)((Value >> 24) & 0xFF); }

	/** Decodes back to its 4 characters, for logging/diagnostics only — comparisons should use == against another FSWGIffTag. */
	FString ToString() const
	{
		const TCHAR Chars[5] =
		{
			(TCHAR)((Value >> 24) & 0xFF),
			(TCHAR)((Value >> 16) & 0xFF),
			(TCHAR)((Value >> 8) & 0xFF),
			(TCHAR)(Value & 0xFF),
			0
		};
		return FString(Chars);
	}
};

/** Builds an FSWGIffTag from 4 characters at compile time, e.g. SWG_IFF_TAG('F','O','R','M'). */
#define SWG_IFF_TAG(A, B, C, D) FSWGIffTag( (uint32(uint8(A)) << 24) | (uint32(uint8(B)) << 16) | (uint32(uint8(C)) << 8) | uint32(uint8(D)) )

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
	FSWGIffTag Tag;      // "FORM" or a 4-char leaf tag (DATA, CRCT, STRT, STNG, PCNT, XXXX, ...)
	FSWGIffTag FormType; // only set when Tag == SWG_IFF_TAG('F','O','R','M') (e.g. CSTB, 0000, SCOT, DERV)
	int32 DataOffset = 0;
	int32 DataSize = 0;

	bool IsForm() const { return Tag == SWG_IFF_TAG('F', 'O', 'R', 'M'); }
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
	bool FindForm(FSWGIffTag FormType, FSWGIffChunk& OutChunk) const;

	/** Finds the first FORM child with the given FormType among Parent's direct children. */
	bool FindChildForm(const FSWGIffChunk& Parent, FSWGIffTag FormType, FSWGIffChunk& OutChunk) const;

	/** Finds the first leaf chunk with the given Tag among Parent's direct children. */
	bool FindChildChunk(const FSWGIffChunk& Parent, FSWGIffTag Tag, FSWGIffChunk& OutChunk) const;

	/** All FORM children (any FormType) among Parent's direct children. */
	TArray<FSWGIffChunk> FindChildForms(const FSWGIffChunk& Parent) const;

	/** All direct leaf children with a given tag, in file order (unlike FindChildChunk, which only returns the first). */
	TArray<FSWGIffChunk> FindAllChildChunks(const FSWGIffChunk& Parent, FSWGIffTag Tag) const;

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
	bool FindFormRecursive(int32 Pos, int32 End, FSWGIffTag FormType, FSWGIffChunk& OutChunk) const;

	static FSWGIffTag ReadTag(const uint8* Bytes);

	static int32 ReadInt32BE(const uint8* Bytes);

	TArray<uint8> Data;
};
