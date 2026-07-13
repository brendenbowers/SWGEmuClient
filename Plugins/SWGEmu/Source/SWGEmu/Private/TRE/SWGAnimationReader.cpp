#include "TRE/SWGAnimationReader.h"
#include "Common/SWGWorldScale.h"

// Comma-separated bone-name substrings; set via swg.DumpAnsAnimation
// (USWGMeshGeneratorSubsystem) to log per-axis scale/swing for those bones. Empty = no logging.
SWGEMU_API FString GSWGDebugAnsBoneFilter;

// CKAT per-axis scale slope divisor — see DecodeQchnChunkCompressed.
SWGEMU_API float GSWGCkatScaleDivisor = 256.0f;
static FAutoConsoleVariableRef CVarSWGCkatScaleDivisor(
	TEXT("swg.CkatScaleDivisor"), GSWGCkatScaleDivisor,
	TEXT("Divisor for CKAT per-axis quantization half-range: (scaleByte-160)/divisor. Default 256. Re-decode with swg.ReloadAnim after changing."));

namespace
{
	float AnimReadFloatLE(const uint8* Data, int32 Offset)
	{
		float Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(float));
		return Value;
	}

	uint16 AnimReadUInt16LE(const uint8* Data, int32 Offset)
	{
		uint16 Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(uint16));
		return Value;
	}

	uint32 AnimReadUInt32LE(const uint8* Data, int32 Offset)
	{
		uint32 Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(uint32));
		return Value;
	}

	double AnimReadDoubleLE(const uint8* Data, int32 Offset)
	{
		double Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(double));
		return Value;
	}

	// Same Y-up (model space) -> Z-up (UE) axis swap and meters -> 100-units
	// scale as SWGMeshReader.cpp/SWGSkeletonReader.cpp — root translation
	// deltas are authored in the same convention as everything else this
	// codebase reads from these files.
}

bool FSWGAnimationReader::FindChildForm(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& FormType, FSWGIffChunk& OutChunk)
{
	for (const FSWGIffChunk& Child : Reader.ReadChildren(Parent))
	{
		if (Child.IsForm() && Child.FormType == FormType)
		{
			OutChunk = Child;
			return true;
		}
	}
	return false;
}

bool FSWGAnimationReader::FindChildChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& Tag, FSWGIffChunk& OutChunk)
{
	for (const FSWGIffChunk& Child : Reader.ReadChildren(Parent))
	{
		if (!Child.IsForm() && Child.Tag == Tag)
		{
			OutChunk = Child;
			return true;
		}
	}
	return false;
}

TArray<FSWGIffChunk> FSWGAnimationReader::FindChildForms(const FSWGIffReader& Reader, const FSWGIffChunk& Parent)
{
	TArray<FSWGIffChunk> Result;
	for (const FSWGIffChunk& Child : Reader.ReadChildren(Parent))
	{
		if (Child.IsForm())
			Result.Add(Child);
	}
	return Result;
}

TArray<FSWGIffChunk> FSWGAnimationReader::FindAllChildChunks(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& Tag)
{
	TArray<FSWGIffChunk> Result;
	for (const FSWGIffChunk& Child : Reader.ReadChildren(Parent))
	{
		if (!Child.IsForm() && Child.Tag == Tag)
		{
			Result.Add(Child);
		}
	}
	return Result;
}

FQuat FSWGAnimationReader::DecodeCompressedQuaternion(uint32 Value, float ScaleX, float ScaleY, float ScaleZ)
{
	// NOT "smallest three": stores the quaternion's VECTOR components and
	// reconstructs W = sqrt(1-x²-y²-z²). Layout: [2 unused bits][10-bit Xq]
	// [10-bit Yq][10-bit Zq], each mapped [0,1023]->[-1,1] and scaled by that
	// axis's per-channel half-range (ScaleX/Y/Z, decoded from the QCHN header).
	const uint32 X10 = (Value >> 20) & 0x3FFu;
	const uint32 Y10 = (Value >> 10) & 0x3FFu;
	const uint32 Z10 = Value & 0x3FFu;

	const double X = ((X10 / 1023.0) * 2.0 - 1.0) * ScaleX;
	const double Y = ((Y10 / 1023.0) * 2.0 - 1.0) * ScaleY;
	const double Z = ((Z10 / 1023.0) * 2.0 - 1.0) * ScaleZ;
	const double W = FMath::Sqrt(FMath::Max(0.0, 1.0 - (X * X + Y * Y + Z * Z)));

	// Y/Z-swap AND conjugate (negate the vector part): (x,y,z,w) -> (-x,-z,-y,w).
	// Proper quaternion basis change for SWG's Y-up -> UE's Z-up (swap alone is
	// an improper map, off by an inversion). FSWGSkeletonReader's
	// SkeletonReadRotationLE keeps the swap-only (x,z,y,w) form for BPRO/RPRE/
	// RPST since .skt quats use the opposite (conjugated) convention from .ans.
	return FQuat(-(float)X, -(float)Z, -(float)Y, (float)W);
}

void FSWGAnimationReader::DecodeQchnChunkCompressed(const FSWGIffReader& Reader, const FSWGIffChunk& Qchn, FSWGAnimationBoneTrack& OutTrack)
{
	const uint8* Data = Reader.GetChunkData(Qchn);
	const int32 Size = Qchn.DataSize;

	// Fully-decoded QCHN layout (see SWGAnimationReader.h):
	//   [uint16 sample-count][3 per-axis scale bytes], then `sample-count`
	//   6-byte records [uint16 frame][uint32 quat], the first being frame 0.
	// The 3 scale bytes are this channel's per-axis (X,Y,Z) quantization
	// half-range: half-range = max(0, byte - 160) / 256. Byte 160 (~0xA0) is
	// the zero-motion baseline, so a still axis (byte <= 160) decodes to
	// exactly 0. DecodeCompressedQuaternion stores X,Y,Z and rebuilds W.
	constexpr int32 SampleCountOffset = 0;
	constexpr int32 ScaleBytesOffset = 2;   // 3 bytes: [X][Y][Z]
	constexpr int32 Frame0QuatOffset = 7;   // = 5-byte header + frame-0's uint16 index (0)
	constexpr int32 FirstExplicitSampleOffset = 11;

	if (Size < Frame0QuatOffset + 4) return;

	const uint8 RawScaleX = Data[ScaleBytesOffset + 0];
	const uint8 RawScaleY = Data[ScaleBytesOffset + 1];
	const uint8 RawScaleZ = Data[ScaleBytesOffset + 2];
	// The /256 slope is unconfirmed (still joints decode to identity under any
	// slope, so it's not directly measurable) — kept as a live-tunable CVar so
	// it can be calibrated via swg.ReloadAnim instead of a rebuild per guess.
	const float Divisor = FMath::Max(1.0f, GSWGCkatScaleDivisor);
	const float ScaleX = FMath::Max(0, (int32)RawScaleX - 160) / Divisor;
	const float ScaleY = FMath::Max(0, (int32)RawScaleY - 160) / Divisor;
	const float ScaleZ = FMath::Max(0, (int32)RawScaleZ - 160) / Divisor;

	bool bDump = false;
	if (!GSWGDebugAnsBoneFilter.IsEmpty())
	{
		TArray<FString> Filters;
		GSWGDebugAnsBoneFilter.ParseIntoArray(Filters, TEXT(","), true);
		for (const FString& F : Filters)
		{
			if (OutTrack.BoneName.Contains(F))
			{
				bDump = true;
				break;
			}
		}
	}

	if (bDump)
	{
		UE_LOG(LogTemp, Warning, TEXT("ANSDUMP %s: raw scale bytes X=%d Y=%d Z=%d -> half-range X=%.4f Y=%.4f Z=%.4f"),
			*OutTrack.BoneName, RawScaleX, RawScaleY, RawScaleZ, ScaleX, ScaleY, ScaleZ);
	}

	// The count includes frame 0, which is decoded separately below; the
	// remaining count-1 explicit records are also bounded by the chunk size.
	const int32 ExplicitSampleCount = (int32)AnimReadUInt16LE(Data, SampleCountOffset);
	{
		const uint32 RawValue = AnimReadUInt32LE(Data, Frame0QuatOffset);
		const FQuat Decoded = DecodeCompressedQuaternion(RawValue, ScaleX, ScaleY, ScaleZ);
		OutTrack.Keyframes.Add(0, Decoded);
		if (bDump)
		{
			FVector Axis; float AngleRad;
			Decoded.ToAxisAndAngle(Axis, AngleRad);
			UE_LOG(LogTemp, Warning, TEXT("ANSDUMP %s: frame 0 raw=0x%08X swing=%.2f deg axis=(%.2f,%.2f,%.2f)"),
				*OutTrack.BoneName, RawValue, FMath::RadiansToDegrees(AngleRad), Axis.X, Axis.Y, Axis.Z);
		}
	}

	int32 Pos = FirstExplicitSampleOffset;
	for (int32 i = 1; i < ExplicitSampleCount && Pos + 6 <= Size; ++i)
	{
		const int32 FrameIndex = (int32)AnimReadUInt16LE(Data, Pos);
		const uint32 RawValue = AnimReadUInt32LE(Data, Pos + 2);
		const FQuat Decoded = DecodeCompressedQuaternion(RawValue, ScaleX, ScaleY, ScaleZ);
		OutTrack.Keyframes.Add(FrameIndex, Decoded);
		if (bDump)
		{
			FVector Axis; float AngleRad;
			Decoded.ToAxisAndAngle(Axis, AngleRad);
			UE_LOG(LogTemp, Warning, TEXT("ANSDUMP %s: frame %d raw=0x%08X swing=%.2f deg axis=(%.2f,%.2f,%.2f)"),
				*OutTrack.BoneName, FrameIndex, RawValue, FMath::RadiansToDegrees(AngleRad), Axis.X, Axis.Y, Axis.Z);
		}
		Pos += 6;
	}
}

void FSWGAnimationReader::DecodeQchnChunkRaw(const FSWGIffReader& Reader, const FSWGIffChunk& Qchn, FSWGAnimationBoneTrack& OutTrack)
{
	const uint8* Data = Reader.GetChunkData(Qchn);
	const int32 Size = Qchn.DataSize;

	// [uint32 sample-count][4 unknown bytes] header (8 bytes), then frame-0's
	// quat with no frame-index prefix, then repeating 20-byte
	// [frame:uint32][4×float32 (W,X,Y,Z)] records — see SWGAnimationReader.h.
	constexpr int32 Frame0Offset = 8;
	constexpr int32 FirstExplicitSampleOffset = 24;

	if (Size < Frame0Offset + 16) return;
	{
		const float W = AnimReadFloatLE(Data, Frame0Offset);
		const float X = AnimReadFloatLE(Data, Frame0Offset + 4);
		const float Y = AnimReadFloatLE(Data, Frame0Offset + 8);
		const float Z = AnimReadFloatLE(Data, Frame0Offset + 12);
		// Y/Z swap + conjugate — see DecodeCompressedQuaternion's comment.
		OutTrack.Keyframes.Add(0, FQuat(-X, -Z, -Y, W));
	}

	int32 Pos = FirstExplicitSampleOffset;
	while (Pos + 20 <= Size)
	{
		const int32 FrameIndex = (int32)AnimReadUInt32LE(Data, Pos);
		const float W = AnimReadFloatLE(Data, Pos + 4);
		const float X = AnimReadFloatLE(Data, Pos + 8);
		const float Y = AnimReadFloatLE(Data, Pos + 12);
		const float Z = AnimReadFloatLE(Data, Pos + 16);
		OutTrack.Keyframes.Add(FrameIndex, FQuat(-X, -Z, -Y, W));
		Pos += 20;
	}
}

TMap<int32, float> FSWGAnimationReader::DecodeChnlChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Chnl)
{
	TMap<int32, float> Result;
	const uint8* Data = Reader.GetChunkData(Chnl);
	const int32 Size = Chnl.DataSize;

	// [count:uint32][frame-0 value:float] header (8 bytes), then repeating
	// 8-byte [value:float][frame:uint32] records — value before frame, and
	// frame is a full uint32, not uint16.
	if (Size < 8) return Result;
	Result.Add(0, AnimReadFloatLE(Data, 4));

	int32 Pos = 8;
	while (Pos + 8 <= Size)
	{
		const float Value = AnimReadFloatLE(Data, Pos);
		const int32 FrameIndex = (int32)AnimReadUInt32LE(Data, Pos + 4);
		Result.Add(FrameIndex, Value);
		Pos += 8;
	}
	return Result;
}

void FSWGAnimationReader::DecodeLoctChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Loct, TMap<int32, float>& OutX, TMap<int32, float>& OutY)
{
	const uint8* Data = Reader.GetChunkData(Loct);
	const int32 Size = Loct.DataSize;

	// [total-distance:float][count:uint32] header (8 bytes), then frame-0
	// with no index prefix — [X:float][Y:double] (12 bytes, Y is 8 bytes,
	// not 4 — see this reader's header comment) — then repeating 14-byte
	// [frame:uint16][X:float][Y:double] records.
	if (Size < 20) return;
	OutX.Add(0, AnimReadFloatLE(Data, 8));
	OutY.Add(0, (float)AnimReadDoubleLE(Data, 12));

	int32 Pos = 20;
	while (Pos + 14 <= Size)
	{
		const int32 FrameIndex = (int32)AnimReadUInt16LE(Data, Pos);
		OutX.Add(FrameIndex, AnimReadFloatLE(Data, Pos + 2));
		OutY.Add(FrameIndex, (float)AnimReadDoubleLE(Data, Pos + 6));
		Pos += 14;
	}
}

void FSWGAnimationReader::DecodeRootTranslation(const FSWGIffReader& Reader, const FSWGIffChunk& InnerForm, FSWGAnimationData& OutAnimation)
{
	FSWGIffChunk AtrnForm;
	if (!FindChildForm(Reader, InnerForm, TEXT("ATRN"), AtrnForm)) return;

	const TArray<FSWGIffChunk> ChnlChunks = FindAllChildChunks(Reader, AtrnForm, TEXT("CHNL"));

	FSWGIffChunk LoctChunk;
	const bool bHasLoct = FindChildChunk(Reader, InnerForm, TEXT("LOCT"), LoctChunk);

	TMap<int32, float> HorizontalX, HorizontalY, Vertical;
	if (bHasLoct)
	{
		DecodeLoctChunk(Reader, LoctChunk, HorizontalX, HorizontalY);
		if (ChnlChunks.Num() > 0)
		{
			Vertical = DecodeChnlChunk(Reader, ChnlChunks[0]);
		}
	}
	else if (ChnlChunks.Num() >= 3)
	{
		HorizontalX = DecodeChnlChunk(Reader, ChnlChunks[0]);
		Vertical = DecodeChnlChunk(Reader, ChnlChunks[1]);
		HorizontalY = DecodeChnlChunk(Reader, ChnlChunks[2]);
	}
	else if (ChnlChunks.Num() == 1)
	{
		// Only a single channel and no LOCT — best guess is this is the
		// vertical component alone (matches the walk clip's single-CHNL
		// case when LOCT is also absent, e.g. a shuffle/idle variant).
		Vertical = DecodeChnlChunk(Reader, ChnlChunks[0]);
	}
	else
	{
		return; // no translation data at all — root stays at bind pose
	}

	// Union of every frame index seen across all three channels — a frame
	// missing from one channel just holds that channel's most recent value
	// (same "hold" behavior as BuildDenseRotationTrack's sparse interpolation,
	// but simplified here to nearest-known since these curves are dense/near-
	// per-frame in every sample seen so far).
	TSet<int32> AllFrames;
	for (const auto& Pair : HorizontalX) AllFrames.Add(Pair.Key);
	for (const auto& Pair : HorizontalY) AllFrames.Add(Pair.Key);
	for (const auto& Pair : Vertical) AllFrames.Add(Pair.Key);

	auto SampleHold = [](const TMap<int32, float>& Map, int32 Frame) -> float
	{
		if (Map.Num() == 0) return 0.0f;
		if (const float* Exact = Map.Find(Frame)) return *Exact;
		float Best = 0.0f;
		int32 BestFrame = -1;
		for (const auto& Pair : Map)
		{
			if (Pair.Key <= Frame && Pair.Key > BestFrame)
			{
				BestFrame = Pair.Key;
				Best = Pair.Value;
			}
		}
		return BestFrame >= 0 ? Best : Map.CreateConstIterator()->Value;
	};

	// Diagnostic (swg.DumpAnsAnimation): the horizontal channels encode the
	// clip's authored travel direction in file space — logging first/last per
	// channel answers WHICH file axis the character walks along, which pins
	// down the animation frame vs. the mesh's visual forward independently of
	// any bone-rotation decoding.
	if (!GSWGDebugAnsBoneFilter.IsEmpty())
	{
		auto NetOf = [](const TMap<int32, float>& Map) -> FVector2D
		{
			if (Map.Num() == 0) return FVector2D::ZeroVector;
			int32 MinF = TNumericLimits<int32>::Max(), MaxF = TNumericLimits<int32>::Min();
			for (const auto& Pair : Map) { MinF = FMath::Min(MinF, Pair.Key); MaxF = FMath::Max(MaxF, Pair.Key); }
			return FVector2D(Map[MinF], Map[MaxF]);
		};
		const FVector2D NX = NetOf(HorizontalX), NZ = NetOf(HorizontalY), NV = NetOf(Vertical);
		UE_LOG(LogTemp, Warning, TEXT("ANSDUMP-LOCT: hasLOCT=%d samples X=%d Z=%d V=%d | fileX first=%.4f last=%.4f net=%.4f | fileZ first=%.4f last=%.4f net=%.4f | fileY(up) first=%.4f last=%.4f net=%.4f"),
			bHasLoct ? 1 : 0, HorizontalX.Num(), HorizontalY.Num(), Vertical.Num(),
			NX.X, NX.Y, NX.Y - NX.X, NZ.X, NZ.Y, NZ.Y - NZ.X, NV.X, NV.Y, NV.Y - NV.X);
	}

	for (int32 Frame : AllFrames)
	{
		const float RawY = SampleHold(Vertical, Frame); // file's Y (up) axis
		// In-place preview: keep only the vertical bob and drop the horizontal
		// advance (HorizontalX/Y), which would otherwise slide the mesh forward
		// and snap back at the loop seam since the actor isn't driven by root motion.
		OutAnimation.RootTranslationDeltas.Add(Frame, FVector(0.0f, 0.0f, RawY) * SWGWorldScale);
	}
}

bool FSWGAnimationReader::ReadAnimation(const FSWGIffReader& Reader, FSWGAnimationData& OutAnimation)
{
	if (!Reader.IsValid()) return false;

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0 || !TopLevel[0].IsForm()) return false;

	// Two distinct top-level encodings — see this file's header comment.
	const bool bIsCompressed = TopLevel[0].FormType == TEXT("CKAT");
	const bool bIsRaw = TopLevel[0].FormType == TEXT("KFAT");
	if (!bIsCompressed && !bIsRaw) return false;

	const FSWGIffChunk& OuterForm = TopLevel[0];

	// Same version-tag-drift pattern as the mesh/skeleton readers.
	const TArray<FSWGIffChunk> VersionForms = FindChildForms(Reader, OuterForm);
	if (VersionForms.Num() == 0) return false;
	const FSWGIffChunk& InnerForm = VersionForms[0];

	FSWGIffChunk InfoChunk;
	if (!FindChildChunk(Reader, InnerForm, TEXT("INFO"), InfoChunk)) return false;

	const uint8* InfoData = Reader.GetChunkData(InfoChunk);
	int32 AnimatedBoneCount = 0;
	OutAnimation.FrameRate = AnimReadFloatLE(InfoData, 0);
	if (bIsCompressed)
	{
		// CKAT: fields are packed as uint16 (16-byte INFO total).
		if (InfoChunk.DataSize < 10) return false;
		OutAnimation.FrameCount = (int32)AnimReadUInt16LE(InfoData, 4);
		AnimatedBoneCount = (int32)AnimReadUInt16LE(InfoData, 8);
	}
	else
	{
		// KFAT: same fields, but each padded out to uint32 (28-byte INFO total).
		if (InfoChunk.DataSize < 16) return false;
		OutAnimation.FrameCount = (int32)AnimReadUInt32LE(InfoData, 4);
		AnimatedBoneCount = (int32)AnimReadUInt32LE(InfoData, 12);
	}

	FSWGIffChunk XfrmForm, ArotForm;
	if (!FindChildForm(Reader, InnerForm, TEXT("XFRM"), XfrmForm)) return false;
	if (!FindChildForm(Reader, InnerForm, TEXT("AROT"), ArotForm)) return false;

	const TArray<FSWGIffChunk> QchnChunks = FindAllChildChunks(Reader, ArotForm, TEXT("QCHN"));
	if (QchnChunks.Num() != AnimatedBoneCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationReader: expected %d animated bone(s) per INFO, found %d QCHN chunk(s) — bone/track mapping may be wrong"),
			AnimatedBoneCount, QchnChunks.Num());
	}

	int32 NextQchnIndex = 0;
	for (const FSWGIffChunk& Xfin : Reader.ReadChildren(XfrmForm))
	{
		if (Xfin.IsForm() || Xfin.Tag != TEXT("XFIN")) continue;

		const uint8* Data = Reader.GetChunkData(Xfin);
		const int32 Size = Xfin.DataSize;

		int32 NameEnd = 0;
		while (NameEnd < Size && Data[NameEnd] != 0) ++NameEnd;
		if (NameEnd >= Size) continue; // no null terminator found — malformed entry

		const FString BoneName = FString::ConstructFromPtrSize((const ANSICHAR*)Data, NameEnd);
		const int32 HasTrackOffset = NameEnd + 1;
		if (HasTrackOffset >= Size) continue;
		const bool bHasTrack = Data[HasTrackOffset] != 0;

		if (!bHasTrack) continue; // stays at the skeleton's bind pose for this clip

		if (!QchnChunks.IsValidIndex(NextQchnIndex))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationReader: ran out of QCHN chunks at bone '%s' — truncating"), *BoneName);
			break;
		}

		FSWGAnimationBoneTrack Track;
		Track.BoneName = BoneName;
		if (bIsCompressed)
		{
			DecodeQchnChunkCompressed(Reader, QchnChunks[NextQchnIndex], Track);
		}
		else
		{
			DecodeQchnChunkRaw(Reader, QchnChunks[NextQchnIndex], Track);
		}
		OutAnimation.BoneTracks.Add(MoveTemp(Track));
		++NextQchnIndex;
	}

	DecodeRootTranslation(Reader, InnerForm, OutAnimation);

	return OutAnimation.BoneTracks.Num() > 0;
}
