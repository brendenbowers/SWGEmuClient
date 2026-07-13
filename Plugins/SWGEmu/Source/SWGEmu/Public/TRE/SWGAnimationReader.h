#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

// Temporary/diagnostic — see SWGAnimationReader.cpp's definition.
extern SWGEMU_API FString GSWGDebugAnsBoneFilter;

/** One bone's decoded rotation keyframes — sparse (only frames actually present in the file). */
struct FSWGAnimationBoneTrack
{
	FString BoneName; // as stored in the file's XFIN entries (lowercase) — matches FSWGMeshData::BoneNames' convention
	TMap<int32, FQuat> Keyframes; // frame index -> rotation; frame 0 is always present for an animated bone
};

struct FSWGAnimationData
{
	float FrameRate = 30.0f;
	int32 FrameCount = 0;
	TArray<FSWGAnimationBoneTrack> BoneTracks; // only bones this clip actually animates — others should keep the skeleton's bind pose throughout

	// Root bone's translation delta from its bind pose, per frame — sparse,
	// already converted to UE space (Y/Z swap + meters->100uu, matching every
	// other position value in this codebase). Empty if the clip has no
	// FORM ATRN/LOCT data at all (root stays at its bind pose translation).
	// See this file's class comment for the two source encodings this comes
	// from (CHNL-only vs CHNL+LOCT).
	TMap<int32, FVector> RootTranslationDeltas;
};

/**
 * Parses SWG's .ans keyframe animation format (rotation tracks only; root/bone
 * translation FORM ATRN/CHNL not yet decoded, so imported clips play in place).
 * Undocumented format, reverse-engineered like FSWGSkeletonReader.
 *
 * Two top-level encodings, same outer shape (INFO/XFRM/AROT+QCHN/SROT/ATRN+
 * CHNL/STRN/MSGS): FORM CKAT (compressed quats, 4 bytes/key) and FORM KFAT
 * (raw 4×float32 quats, 16 bytes/key). XFIN bone descriptors are
 * [name, null-terminated][hasTrack:uint8]; hasTrack=0 keeps bind pose. One
 * QCHN per hasTrack=1 bone, in XFRM order. SROT (non-hasTrack rotation) and
 * ATRN/CHNL/STRN (translation) and MSGS (named events) are not decoded.
 *
 * CKAT QCHN: [uint16 sample-count][3 per-axis scale bytes], then
 * sample-count 6-byte records [frame:uint16][quat:uint32]. The quat stores
 * only the VECTOR part — [2 unused bits][10-bit Xq][10-bit Yq][10-bit Zq],
 * each mapped [0,1023]->[-1,1] and scaled by that axis's half-range
 * (half-range = max(0, scaleByte-160)/256); W = sqrt(1-x²-y²-z²). This is
 * NOT "smallest three" — the top 2 bits are unused, not a largest-index.
 *
 * KFAT QCHN: [uint32 sample-count][4 unknown bytes] header, then frame-0's
 * (W,X,Y,Z) with no frame-index prefix, then 20-byte
 * [frame:uint32][4×float32 (W,X,Y,Z)] records.
 *
 * Root translation, in different combinations per file (idle clips: 3×
 * CHNL, no LOCT; locomotion clips: 1× CHNL + LOCT):
 *   CHNL (single-float channel, e.g. vertical bob): [uint32 count][frame-0
 *     value:float] header, then 6-byte [frame:uint16][value:float] records.
 *   LOCT (2D horizontal offset): [total-distance:float][count:uint32]
 *     header, then frame-0 [X:float][Y:double] (Y is 8 bytes), then 14-byte
 *     [frame:uint16][X:float][Y:double] records. Semantic meaning of X/Y
 *     (offset vs velocity, axis mapping) unconfirmed — only the byte layout is.
 * When both are present, LOCT supplies horizontal (X,Y) and CHNL supplies
 * vertical; with only 3× CHNL (no LOCT), each is read as one of the 3 axes.
 */
class SWGEMU_API FSWGAnimationReader
{
public:
	/** Parses a .ans buffer (FORM CKAT or FORM KFAT). Returns false if unrecognized. */
	static bool ReadAnimation(const FSWGIffReader& Reader, FSWGAnimationData& OutAnimation);

private:
	static bool FindChildForm(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& FormType, FSWGIffChunk& OutChunk);
	static bool FindChildChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& Tag, FSWGIffChunk& OutChunk);
	static TArray<FSWGIffChunk> FindChildForms(const FSWGIffReader& Reader, const FSWGIffChunk& Parent);

	/** All direct leaf children with a given tag, in file order (unlike FindChildChunk, which only returns the first). */
	static TArray<FSWGIffChunk> FindAllChildChunks(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& Tag);

	/** Decodes one CKAT compressed quaternion: stores the vector (X,Y,Z) and
	 *  reconstructs the scalar W. ScaleX/Y/Z are the channel's per-axis
	 *  quantization half-ranges (from the QCHN header scale bytes) — see this
	 *  file's class comment. */
	static FQuat DecodeCompressedQuaternion(uint32 Value, float ScaleX, float ScaleY, float ScaleZ);

	/** Decodes a single QCHN chunk's sparse keyframes into OutTrack.Keyframes — CKAT's compressed encoding. */
	static void DecodeQchnChunkCompressed(const FSWGIffReader& Reader, const FSWGIffChunk& Qchn, FSWGAnimationBoneTrack& OutTrack);

	/** Decodes a single QCHN chunk's sparse keyframes into OutTrack.Keyframes — KFAT's raw-float encoding. */
	static void DecodeQchnChunkRaw(const FSWGIffReader& Reader, const FSWGIffChunk& Qchn, FSWGAnimationBoneTrack& OutTrack);

	/** Decodes a single CHNL chunk (one scalar channel) into a sparse frame->value map. */
	static TMap<int32, float> DecodeChnlChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Chnl);

	/** Decodes a LOCT chunk into sparse frame->(X,Y) maps — see this file's class comment. */
	static void DecodeLoctChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Loct, TMap<int32, float>& OutX, TMap<int32, float>& OutY);

	/** Parses FORM ATRN's CHNL children plus an optional sibling LOCT chunk into OutAnimation.RootTranslationDeltas. */
	static void DecodeRootTranslation(const FSWGIffReader& Reader, const FSWGIffChunk& InnerForm, FSWGAnimationData& OutAnimation);
};
