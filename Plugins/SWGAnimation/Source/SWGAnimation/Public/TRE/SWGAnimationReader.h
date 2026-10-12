#pragma once

#include "CoreMinimal.h"
#include "TRE/SWGIffReader.h"

/** One bone's decoded rotation and translation delta keyframes, in UE space. */
struct FSWGAnimationBoneTrack
{
	FString BoneName; // as stored in the file's XFIN entries (lowercase) — matches FSWGMeshData::BoneNames' convention
	TMap<int32, FQuat> Keyframes; // frame index -> rotation; frame 0 is always present for an animated bone
	TMap<int32, FVector> TranslationDeltas;
};

/** One named MSGS event and the frames it fires on (30 fps frame indices, ascending). */
struct FSWGAnimationEvent
{
	FString Name;
	TArray<int32> Frames;
};

struct FSWGAnimationData
{
	float FrameRate = 30.0f;
	int32 FrameCount = 0;
	TArray<FSWGAnimationBoneTrack> BoneTracks; // only bones this clip actually animates — others should keep the skeleton's bind pose throughout

	// Root bone's translation delta from its bind pose, per frame — sparse,
	// already converted to UE space (axis rotation + meters->100uu, matching every
	// other position value in this codebase). Only vertical bob is applied;
	// horizontal root travel is driven by the actor.
	TMap<int32, FVector> RootTranslationDeltas;

	/**
	 * Ground distance the root travels over the clip's full length, in UE
	 * units, from the LOCT header. Over the clip's duration this gives the
	 * speed it was *authored* to move at, which is where its sample belongs in
	 * a locomotion blend space — otherwise the legs cycle at the authored gait
	 * while the creature travels at a different speed. 0 for a clip with no
	 * LOCT, i.e. one authored in place.
	 */
	float RootTravelDistance = 0.0f;

	/** FORM MSGS named events, in file order. Empty for clips without one. */
	TArray<FSWGAnimationEvent> Events;
};

/**
 * Parses SWG's .ans rotation and translation tracks. Horizontal root motion
 * stays in place; the actor drives locomotion.
 * Undocumented format, reverse-engineered like FSWGSkeletonReader.
 *
 * Two top-level encodings, same outer shape (INFO/XFRM/AROT+QCHN/SROT/ATRN+
 * CHNL/STRN/MSGS): FORM CKAT (compressed quats, 4 bytes/key) and FORM KFAT
 * (raw 4×float32 quats, 16 bytes/key). XFIN bone descriptors are
 * [name, null-terminated][hasTrack:uint8][channelIndex:uint16 (CKAT) or
 * uint32 (KFAT)]. Each index selects a QCHN for hasTrack=1; a hasTrack=0
 * bone's index instead selects a constant rotation from SROT, and only falls
 * back to the bind pose when SROT has no such entry.
 *
 * Sibling order under the version form is XFRM, AROT, SROT, ATRN, STRN — SROT
 * is NOT inside FORM AROT. XFIN then stores a translation mask (uint8 CKAT,
 * uint32 KFAT) and three scalar indices (uint16 CKAT, uint32 KFAT). Bits
 * 0x08/0x10/0x20 select animated X/Y/Z from ATRN's CHNL array; clear bits
 * select static float deltas from STRN. Values add to the skeleton bind pose.
 *
 * FORM MSGS (a version-form child after STRN; 2802 of 7277 retail clips):
 * INFO = [uint16 message count], then one MESG per message, laid out
 * [uint16 frameCount][name, null-terminated][frameCount x uint16 frame]. Frames
 * are strictly ascending, in the clip's own frame rate (30 fps), and may be 0.
 * Identical in CKAT and KFAT; checked on every retail clip (chunk sizes add up
 * exactly, INFO count always equals the MESG count). Names seen: event_footstep,
 * event_vocalize, event_hit*, event_attack*, fireN, sound_<name>, hpevent_*.
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
 * CHNL is [count][frame,float]*. Count/frame are uint16 in CKAT, uint32 in
 * KFAT. Root bob uses the root XFIN's Y channel, not the first CHNL. LOCT's
 * first float is total ground travel, retained for authored gait speed.
 */
class SWGANIMATION_API FSWGAnimationReader
{
public:
	/** Parses a .ans buffer (FORM CKAT or FORM KFAT). Returns false if unrecognized. */
	static bool ReadAnimation(const FSWGIffReader& Reader, FSWGAnimationData& OutAnimation);

private:
	/** Decodes one CKAT compressed quaternion from its four independent bytes
	 *  (X,Y,Z,W). ScaleX/Y/Z are the channel's per-axis quantization
	 *  half-ranges (from the QCHN header scale bytes); W has no format byte
	 *  and decodes unscaled. Result is normalized to correct quantization
	 *  residue. See this file's class comment. */
	static FQuat DecodeCompressedQuaternion(uint8 RawX, uint8 RawY, uint8 RawZ, uint8 RawW, float ScaleX, float ScaleY, float ScaleZ);

	/** Decodes a single QCHN chunk's sparse keyframes into OutTrack.Keyframes — CKAT's compressed encoding. */
	static void DecodeQchnChunkCompressed(const FSWGIffReader& Reader, const FSWGIffChunk& Qchn, FSWGAnimationBoneTrack& OutTrack);

	/** Decodes a single QCHN chunk's sparse keyframes into OutTrack.Keyframes — KFAT's raw-float encoding. */
	static void DecodeQchnChunkRaw(const FSWGIffReader& Reader, const FSWGIffChunk& Qchn, FSWGAnimationBoneTrack& OutTrack);
	static TArray<FQuat> DecodeStaticRotations(const FSWGIffReader& Reader,	const FSWGIffChunk& Srot, bool bIsCompressed);

	/** Decodes a single CHNL chunk (one scalar channel) into a sparse frame->value map. */
	static TMap<int32, float> DecodeChnlChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Chnl, bool bIsCompressed);

	/** Decodes FORM MSGS (a sibling of ATRN) into OutAnimation.Events; see this file's class comment. */
	static void DecodeEvents(const FSWGIffReader& Reader, const FSWGIffChunk& InnerForm, FSWGAnimationData& OutAnimation);

	/** Retains root vertical bob and LOCT's authored ground travel. */
	static void DecodeRootTranslation(const FSWGIffReader& Reader, const FSWGIffChunk& InnerForm, FSWGAnimationData& OutAnimation);
	static FQuat DecodeCompressedQuaternion(
		uint32 PackedValue,
		uint8 FormatX,
		uint8 FormatY,
		uint8 FormatZ);

};
