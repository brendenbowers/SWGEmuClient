#include "TRE/SWGAnimationReader.h"
#include "TRE/SWGIffTags.h"
#include "TRE/SWGIFFChunkReader.h"
#include "Common/SWGWorldScale.h"

// Comma-separated bone-name substrings; set via swg.DumpAnsAnimation
// (USWGMeshGeneratorSubsystem) to log per-axis scale/swing for those bones. Empty = no logging.
SWGANIMATION_API FString GSWGDebugAnsBoneFilter;

namespace
{
	struct FSWGQuatFormatInfo
	{
		uint8 FormatId;
		uint8 BaseIndexMask;
		uint8 BaseShiftCount;
	};

	constexpr FSWGQuatFormatInfo QuatFormatInfos[] =
	{
		{ 0xFE, 0x00, 0 },
		{ 0xFC, 0x01, 1 },
		{ 0xF8, 0x03, 2 },
		{ 0xF0, 0x07, 3 },
		{ 0xE0, 0x0F, 4 },
		{ 0xC0, 0x1F, 5 },
		{ 0x80, 0x3F, 6 },
	};

	bool DecodeQuatFormat(uint8 Format, float& OutBaseValue, float& OutHalfRange)
	{
		for (const FSWGQuatFormatInfo& Info : QuatFormatInfos)
		{
			if ((Format & ~Info.BaseIndexMask) == Info.FormatId)
			{
				const int32 BaseCount = 1 << Info.BaseShiftCount;
				const int32 BaseIndex = Format & Info.BaseIndexMask;

				if (BaseIndex >= BaseCount)
				{
					return false;
				}

				OutHalfRange = 2.0f / (float)(BaseCount + 1);
				OutBaseValue = -1.0f + (float)(BaseIndex + 1) * OutHalfRange;
				return true;
			}
		}

		return false;
	}

	float ExpandQuat11(uint32 Value, uint8 Format)
	{
		float BaseValue = 0.0f;
		float HalfRange = 0.0f;
		if (!DecodeQuatFormat(Format, BaseValue, HalfRange))
		{
			return 0.0f;
		}

		constexpr uint32 SignBit = 0x400;
		constexpr uint32 MagnitudeMask = 0x3FF;
		const float Offset = (float)(Value & MagnitudeMask) * (HalfRange / 1023.0f);

		return (Value & SignBit) ? BaseValue - Offset : BaseValue + Offset;
	}

	float ExpandQuat10(uint32 Value, uint8 Format)
	{
		float BaseValue = 0.0f;
		float HalfRange = 0.0f;
		if (!DecodeQuatFormat(Format, BaseValue, HalfRange))
		{
			return 0.0f;
		}

		constexpr uint32 SignBit = 0x200;
		constexpr uint32 MagnitudeMask = 0x1FF;
		const float Offset = (float)(Value & MagnitudeMask) * (HalfRange / 511.0f);

		return (Value & SignBit) ? BaseValue - Offset : BaseValue + Offset;
	}

	// Same Y-up (model space) -> Z-up (UE) axis swap and meters -> 100-units
	// scale as SWGMeshReader.cpp/SWGSkeletonReader.cpp — root translation
	// deltas are authored in the same convention as everything else this
	// codebase reads from these files.
}

FQuat FSWGAnimationReader::DecodeCompressedQuaternion(
	uint32 PackedValue,
	uint8 FormatX,
	uint8 FormatY,
	uint8 FormatZ)
{
	// Packed layout: X=11 bits, Y=11 bits, Z=10 bits.
	const float X = ExpandQuat11(PackedValue >> 21, FormatX);
	const float Y = ExpandQuat11(PackedValue >> 10, FormatY);
	const float Z = ExpandQuat10(PackedValue, FormatZ);
	const float W = FMath::Sqrt(FMath::Max(0.0f, 1.0f - X * X - Y * Y - Z * Z));

	// Keep the current configurable UE-axis conversion while validating it.
	FQuat Result(-X, -Z, -Y, W);
	Result.Normalize();
	return Result;
}

void FSWGAnimationReader::DecodeQchnChunkCompressed(const FSWGIffReader& Reader, const FSWGIffChunk& Qchn, FSWGAnimationBoneTrack& OutTrack)
{
	FSWGIFFChunkReader QchnReader(Qchn, Reader);

	// CONFIRMED QCHN layout (see DecodeCompressedQuaternion's comment for how
	// this was pinned down): [uint16 sample-count][3 per-axis scale/"format"
	// bytes X,Y,Z], then `sample-count` 6-byte records
	// [uint16 frame][byte CompressedX][byte CompressedY][byte CompressedZ]
	// [byte CompressedW], the first being frame 0. The 3 format bytes are this
	// channel's per-axis (X,Y,Z) quantization half-range: half-range =
	// max(0, byte - 160) / 256. Byte 160 (~0xA0) is the zero-motion baseline,
	// so a still axis (byte <= 160) decodes to exactly 0. W has no format byte
	// and decodes unscaled.
	// TODO: Still seems to be incorrect
	if (!QchnReader.CanRead(5)) return;

	// The count includes frame 0, which is decoded separately below; the
	// remaining count-1 explicit records are also bounded by the chunk size.
	const int32 ExplicitSampleCount = (int32)QchnReader.ReadValueLE<uint16>();
	const uint8 RawScaleX = QchnReader.ReadValueLE<uint8>();
	const uint8 RawScaleY = QchnReader.ReadValueLE<uint8>();
	const uint8 RawScaleZ = QchnReader.ReadValueLE<uint8>();

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
		UE_LOG(LogTemp, Warning, TEXT("ANSDUMP %s: raw scale bytes X=%d Y=%d Z=%d"),
			*OutTrack.BoneName, RawScaleX, RawScaleY, RawScaleZ);
	}

	if (!QchnReader.CanRead(6)) return;

	auto DecodeSample = [&](int32 FrameIndex)
	{
		const uint32 Packed = QchnReader.ReadValueLE<uint32>();
		const FQuat Decoded = DecodeCompressedQuaternion(
			Packed, RawScaleX, RawScaleY, RawScaleZ);
		OutTrack.Keyframes.Add(FrameIndex, Decoded);
		if (bDump)
		{
			FVector Axis; float AngleRad;
			Decoded.ToAxisAndAngle(Axis, AngleRad);
			UE_LOG(LogTemp, Warning, TEXT("ANSDUMP %s: frame %d raw=(%d,%d,%d,%d) swing=%.2f deg axis=(%.2f,%.2f,%.2f)"),
				*OutTrack.BoneName, FrameIndex, Packed & 0xFF, (Packed >> 8) & 0xFF, (Packed >> 16) & 0xFF, (Packed >> 24) & 0xFF,
				FMath::RadiansToDegrees(AngleRad), Axis.X, Axis.Y, Axis.Z);
		}
	};

	QchnReader.Skip<uint16>(); // frame-0's explicit frame index (always 0)
	DecodeSample(0);

	for (int32 i = 1; i < ExplicitSampleCount && QchnReader.CanRead(6); ++i)
	{
		const int32 FrameIndex = (int32)QchnReader.ReadValueLE<uint16>();
		DecodeSample(FrameIndex);
	}
}

void FSWGAnimationReader::DecodeQchnChunkRaw(const FSWGIffReader& Reader, const FSWGIffChunk& Qchn, FSWGAnimationBoneTrack& OutTrack)
{
	FSWGIFFChunkReader QchnReader(Qchn, Reader);

	// [uint32 sample-count][4 unknown bytes] header (8 bytes), then frame-0's
	// quat with no frame-index prefix, then repeating 20-byte
	// [frame:uint32][4×float32 (W,X,Y,Z)] records — see SWGAnimationReader.h.
	if (!QchnReader.CanRead(8 + 16)) return;
	QchnReader.Skip(8);

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

	auto DecodeSample = [&](int32 FrameIndex)
	{
		const float W = QchnReader.ReadValueLE<float>();
		const float X = QchnReader.ReadValueLE<float>();
		const float Y = QchnReader.ReadValueLE<float>();
		const float Z = QchnReader.ReadValueLE<float>();
		// Y/Z swap + conjugate — see DecodeCompressedQuaternion's comment.
		const FQuat Decoded(-X, -Z, -Y, W);
		OutTrack.Keyframes.Add(FrameIndex, Decoded);
		if (bDump)
		{
			FVector Axis; float AngleRad;
			Decoded.ToAxisAndAngle(Axis, AngleRad);
			UE_LOG(LogTemp, Warning, TEXT("ANSDUMP-KFAT %s: frame %d swing=%.2f deg axis=(%.2f,%.2f,%.2f)"),
				*OutTrack.BoneName, FrameIndex, FMath::RadiansToDegrees(AngleRad), Axis.X, Axis.Y, Axis.Z);
		}
	};

	DecodeSample(0);

	while (QchnReader.CanRead(20))
	{
		const int32 FrameIndex = (int32)QchnReader.ReadValueLE<uint32>();
		DecodeSample(FrameIndex);
	}
}

TArray<FQuat> FSWGAnimationReader::DecodeStaticRotations(
	const FSWGIffReader& Reader,
	const FSWGIffChunk& Srot,
	bool bIsCompressed)
{
	TArray<FQuat> Result;
	FSWGIFFChunkReader SrotReader(Srot, Reader);

	if (bIsCompressed)
	{
		while (SrotReader.CanRead(7))
		{
			const uint8 FormatX = SrotReader.ReadValueLE<uint8>();
			const uint8 FormatY = SrotReader.ReadValueLE<uint8>();
			const uint8 FormatZ = SrotReader.ReadValueLE<uint8>();
			const uint32 Packed = SrotReader.ReadValueLE<uint32>();

			Result.Add(DecodeCompressedQuaternion(Packed, FormatX, FormatY, FormatZ));
		}
	}
	else
	{
		// KFAT SROT: repeated W, X, Y, Z float quaternions.
		while (SrotReader.CanRead(16))
		{
			const float W = SrotReader.ReadValueLE<float>();
			const float X = SrotReader.ReadValueLE<float>();
			const float Y = SrotReader.ReadValueLE<float>();
			const float Z = SrotReader.ReadValueLE<float>();
			Result.Add(FQuat(-X, -Z, -Y, W));
		}
	}

	return Result;
}

TMap<int32, float> FSWGAnimationReader::DecodeChnlChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Chnl)
{
	TMap<int32, float> Result;
	FSWGIFFChunkReader ChnlReader(Chnl, Reader);

	// [count:uint32][frame-0 value:float] header (8 bytes), then repeating
	// 8-byte [value:float][frame:uint32] records — value before frame, and
	// frame is a full uint32, not uint16.
	if (!ChnlReader.CanRead(8)) return Result;
	ChnlReader.Skip<uint32>(); // count — unused, frame indices below are explicit
	Result.Add(0, ChnlReader.ReadValueLE<float>());

	while (ChnlReader.CanRead(8))
	{
		const float Value = ChnlReader.ReadValueLE<float>();
		const int32 FrameIndex = (int32)ChnlReader.ReadValueLE<uint32>();
		Result.Add(FrameIndex, Value);
	}
	return Result;
}

void FSWGAnimationReader::DecodeLoctChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Loct, TMap<int32, float>& OutX, TMap<int32, float>& OutY)
{
	FSWGIFFChunkReader LoctReader(Loct, Reader);

	// [total-distance:float][count:uint32] header (8 bytes), then frame-0
	// with no index prefix — [X:float][Y:double] (12 bytes, Y is 8 bytes,
	// not 4 — see this reader's header comment) — then repeating 14-byte
	// [frame:uint16][X:float][Y:double] records.
	if (!LoctReader.CanRead(20)) return;
	LoctReader.Skip(8); // total-distance + count — unused
	OutX.Add(0, LoctReader.ReadValueLE<float>());
	OutY.Add(0, (float)LoctReader.ReadValueLE<double>());

	while (LoctReader.CanRead(14))
	{
		const int32 FrameIndex = (int32)LoctReader.ReadValueLE<uint16>();
		OutX.Add(FrameIndex, LoctReader.ReadValueLE<float>());
		OutY.Add(FrameIndex, (float)LoctReader.ReadValueLE<double>());
	}
}

void FSWGAnimationReader::DecodeRootTranslation(const FSWGIffReader& Reader, const FSWGIffChunk& InnerForm, FSWGAnimationData& OutAnimation)
{
	FSWGIffChunk AtrnForm;
	if (!Reader.FindChildForm(InnerForm, SWG_IFF_TAG('A','T','R','N'), AtrnForm)) return;

	const TArray<FSWGIffChunk> ChnlChunks = Reader.FindAllChildChunks(AtrnForm, SWG_IFF_TAG('C','H','N','L'));

	FSWGIffChunk LoctChunk;
	const bool bHasLoct = Reader.FindChildChunk(InnerForm, SWG_IFF_TAG('L','O','C','T'), LoctChunk);

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
	if (TopLevel.Num() == 0 || !TopLevel[0].IsForm())
	{
		return false;
	}

	// Two distinct top-level encodings — see this file's header comment.
	const bool bIsCompressed = TopLevel[0].FormType == SWG_IFF_TAG('C','K','A','T');
	const bool bIsRaw = TopLevel[0].FormType == SWG_IFF_TAG('K','F','A','T');
	if (!bIsCompressed && !bIsRaw)
	{
		return false;
	}

	const FSWGIffChunk& OuterForm = TopLevel[0];

	// Same version-tag-drift pattern as the mesh/skeleton readers.
	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(OuterForm);
	if (VersionForms.Num() == 0)
	{
		return false;
	}
	const FSWGIffChunk& InnerForm = VersionForms[0];

	FSWGIffChunk InfoChunk;
	if (!Reader.FindChildChunk(InnerForm, SWGIffTags::Info, InfoChunk))
	{
		return false;
	}


	int32 AnimatedBoneCount = 0;
	{
		FSWGIFFChunkReader InfoReader(InfoChunk, Reader);
		InfoReader.ReadValueLE<float>(OutAnimation.FrameRate);

		if (bIsCompressed)
		{
			// CKAT: fields are packed as uint16 (16-byte INFO total).
			if (InfoChunk.DataSize < 10)
			{
				return false;
			}

			OutAnimation.FrameCount = InfoReader.ReadValueLE<uint16>();
			InfoReader.Skip(2); // reserved — AnimatedBoneCount is at offset 8, not 6
			AnimatedBoneCount = InfoReader.ReadValueLE<uint16>();

		}
		else
		{
			// KFAT: same fields, but each padded out to uint32 (28-byte INFO total).
			if (InfoChunk.DataSize < 16)
			{
				return false;
			}

			OutAnimation.FrameCount = InfoReader.ReadValueLE<uint32>();
			InfoReader.Skip(4); // reserved — AnimatedBoneCount is at offset 12, not 8
			AnimatedBoneCount = InfoReader.ReadValueLE<uint32>();
		}

	}

	FSWGIffChunk XfrmForm, ArotForm, SrotChunk;
	if (!Reader.FindChildForm(InnerForm, SWG_IFF_TAG('X','F','R','M'), XfrmForm)) return false;
	if (!Reader.FindChildForm(InnerForm, SWG_IFF_TAG('A','R','O','T'), ArotForm)) return false;

	const TArray<FSWGIffChunk> QchnChunks = Reader.FindAllChildChunks(ArotForm, SWG_IFF_TAG('Q','C','H','N'));
	const bool bHasSrot =
		Reader.FindChildChunk(ArotForm, SWG_IFF_TAG('S','R','O','T'), SrotChunk);

	const TArray<FQuat> StaticRotations = bHasSrot
		? DecodeStaticRotations(Reader, SrotChunk, bIsCompressed)
		: TArray<FQuat>();
	if (QchnChunks.Num() != AnimatedBoneCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationReader: expected %d animated bone(s) per INFO, found %d QCHN chunk(s) — bone/track mapping may be wrong"),
			AnimatedBoneCount, QchnChunks.Num());
	}

	for (const FSWGIffChunk& Xfin : Reader.ReadChildren(XfrmForm))
	{
		if (Xfin.IsForm() || Xfin.Tag != SWG_IFF_TAG('X','F','I','N'))
		{
			continue;
		}

		FSWGIFFChunkReader XfinReader(Xfin, Reader);
		const FString BoneName = XfinReader.ReadTerminiatedString();
		if (XfinReader.AtEnd())
		{
			continue;
		}

		const bool bHasTrack = XfinReader.ReadValueLE<uint8>() != 0;
		if (!XfinReader.CanRead(bIsCompressed ? 2 : 4))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("FSWGAnimationReader: truncated XFIN rotation index for bone '%s'"),
				*BoneName);
			continue;
		}

		const int32 RotationChannelIndex = bIsCompressed
			? XfinReader.ReadValueLE<uint16>()
			: XfinReader.ReadValueLE<uint32>();

		FSWGAnimationBoneTrack Track;
		Track.BoneName = BoneName;

		if (bHasTrack)
		{
			if (!QchnChunks.IsValidIndex(RotationChannelIndex))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("FSWGAnimationReader: rotation channel %d for bone '%s' is outside %d QCHN chunk(s)"),
					RotationChannelIndex, *BoneName, QchnChunks.Num());
				continue;
			}

			// XFIN uses the same explicit channel index for both encodings, but
			// their QCHN payload layouts are different. The compressed-channel
			// mapping fix must not route raw KFAT clips through the CKAT decoder.
			if (bIsCompressed)
			{
				DecodeQchnChunkCompressed(Reader, QchnChunks[RotationChannelIndex], Track);
			}
			else
			{
				DecodeQchnChunkRaw(Reader, QchnChunks[RotationChannelIndex], Track);
			}
		}
		else if (StaticRotations.IsValidIndex(RotationChannelIndex))
		{
			// Static rotation: use SROT[RotationChannelIndex].
			Track.Keyframes.Add(0, StaticRotations[RotationChannelIndex]);
		}
		else
		{
			// No static entry: runtime falls back to the skeleton BPRO bind rotation.
			continue;
		}

		OutAnimation.BoneTracks.Add(MoveTemp(Track));
	}

	DecodeRootTranslation(Reader, InnerForm, OutAnimation);

	return OutAnimation.BoneTracks.Num() > 0;
}
