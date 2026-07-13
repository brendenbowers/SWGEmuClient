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
 * Parses SWG's .ans keyframe animation format — rotation tracks only for now
 * (see FSWGAnimationData's comment); root/bone translation (FORM ATRN/CHNL)
 * isn't decoded yet, so imported clips play back in place. Reverse-engineered
 * the same way as FSWGSkeletonReader — no prior documentation existed for
 * this format.
 *
 * Two distinct top-level encodings exist (confirmed against real files —
 * NOT a version number bumped over time, both appear in current content):
 *   FORM CKAT — compressed, "smallest three" packed quaternions (4 bytes/key)
 *   FORM KFAT — uncompressed, raw 4×float32 quaternions (16 bytes/key)
 * Both share the same outer shape (INFO/XFRM/AROT+QCHN/SROT/ATRN+CHNL/STRN/
 * MSGS) and the same XFIN bone-descriptor convention (name, then a hasTrack
 * flag byte right after the null terminator) — only QCHN's internal sample
 * encoding differs, plus INFO/XFIN's exact trailing byte counts (unused by
 * this reader either way).
 *
 * Confirmed common layout:
 *   FORM {CKAT,KFAT} > FORM {0001,0003} (version wrapper):
 *     INFO — [float32 FrameRate][uint16 FrameCount][uint16 TotalBoneCount]
 *            [uint16 AnimatedBoneCount][... unknown trailing fields]
 *     FORM XFRM — one XFIN chunk per skeleton bone, in a fixed order:
 *       XFIN: [name, null-terminated][hasTrack:uint8][unknown trailing bytes]
 *       hasTrack=0 means this bone isn't animated by this clip at all — it
 *       should keep the skeleton's own bind pose for the whole clip.
 *     FORM AROT — one QCHN chunk per hasTrack=1 bone, *in the same relative
 *       order* those bones appear in XFRM (confirmed: AnimatedBoneCount
 *       matches the QCHN chunk count exactly in both encodings).
 *     SROT — static/reference rotation data for non-hasTrack bones. Format
 *       not yet decoded — since hasTrack=0 bones fall back to bind pose
 *       anyway, this isn't currently read.
 *     FORM ATRN/CHNL, STRN — root/bone translation. Not yet decoded.
 *     FORM MSGS — named animation events (e.g. "uhit_hold_l" for combat
 *       timing, "event_breathe" for idles). Not currently read.
 *
 * CKAT's QCHN compression — FULLY DECODED:
 *   [uint16 sample-count][3 per-axis scale bytes], then `sample-count`
 *   6-byte records [frame index: uint16][quat: uint32], the first of which
 *   is frame 0. (chunk size == 5 + sample-count*6 exactly, verified across
 *   every channel of every clip checked.)
 * It is NOT "smallest three". Each 32-bit quat stores the quaternion's three
 * VECTOR components and reconstructs the scalar: [2 unused/flag bits][10-bit
 * Xq][10-bit Yq][10-bit Zq] from high bit to low; each 10-bit field maps
 * [0,1023] to [-1,1] and is multiplied by that AXIS's half-range, decoded
 * from the 3 header scale bytes as half-range = max(0, byte-160)/256 (byte
 * ~160/0xA0 = zero-motion baseline, so a still axis decodes to exactly 0).
 * Then W = sqrt(1 - x^2 - y^2 - z^2). The per-axis scale keeps X/Y/Z small so
 * W is always the dominant component (min W = 0.87 over the walk clip), which
 * is why reconstructing W is exact. The top 2 bits are NOT a largest-index
 * (they disagree with the actual argmax on 515/798 samples) and are ignored.
 *
 * These two facts (vector+reconstruct-W, and per-axis scale) fixed the walk
 * clip. The earlier "smallest three + fixed 1/sqrt(2)" decode scrambled the
 * axis/scalar assignment (the "ball of limbs" tumble) and over-rotated the
 * root to ~135deg (whole-body spin). With this decode, still joints
 * (head/clavicle/wrist) are exact identity, the root is a gentle sway, and
 * whole-clip continuity is mean|dot| ~0.99. Derivation in
 * WOOKIEE_ANIMATION_POSE_BUG.md.
 *
 * KFAT's QCHN (raw float): [uint32 explicit-sample-count][4 unknown bytes]
 * header (8 bytes), then frame-0's quat with NO frame-index prefix (implicit,
 * same convention CKAT uses for frame 0) — 16 bytes, [4×float32 (W,X,Y,Z)] —
 * then repeating 20-byte records until the chunk ends: [frame index: uint32]
 * [4×float32 (W,X,Y,Z)]. Verified: first sample's 4 floats sum-of-squares ≈
 * 1.0 (a real unit quaternion), and consecutive frames repeat identical
 * values where the source clip holds a static sub-pose (also confirmed).
 *
 * Root translation — two source chunks, seen in different combinations per
 * file (KFAT idle clips: three CHNL, no LOCT; CKAT locomotion clips: one
 * CHNL + one LOCT):
 *   CHNL (single-float channel, e.g. vertical bob): [uint32 explicit-count]
 *     [frame-0 value: float, embedded directly in the header — no separate
 *     offset] (8-byte header), then repeating 6-byte records until the chunk
 *     ends: [frame index: uint16][value: float]. Verified against a walk
 *     clip's single CHNL: produces a smooth down-then-up bob curve.
 *   LOCT (2D horizontal locomotion offset, only present on some clips):
 *     [total-distance: float][sample count: uint32] header (8 bytes), then
 *     frame-0 with no index prefix — [X: float][Y: double] (12 bytes,
 *     note Y is 8 bytes, not 4) — then repeating 14-byte records until the
 *     chunk ends: [frame index: uint16][X: float][Y: double]. Verified: both
 *     X and Y come out as smooth, bounded, monotonic-ish curves (Y decaying
 *     from ~-0.6 to ~0, X crossing zero) — a plausible horizontal-offset
 *     profile for a walk cycle, unlike every other byte-width hypothesis
 *     tried, which produced huge/NaN garbage values. The precise semantic
 *     meaning (offset vs velocity, and which of X/Y maps to which world
 *     axis) is not conclusively verified — only the byte layout is.
 * When both CHNL and LOCT are present, LOCT supplies the horizontal (X,Y)
 * plane and CHNL supplies the vertical component; when only 3× CHNL are
 * present (no LOCT), they're read directly as the 3 axes.
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
