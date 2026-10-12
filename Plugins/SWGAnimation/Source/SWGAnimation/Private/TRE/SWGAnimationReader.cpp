#include "TRE/SWGAnimationReader.h"
#include "TRE/SWGIffTags.h"
#include "TRE/SWGIFFChunkReader.h"
#include "Common/SWGWorldScale.h"

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

	// Same native -> UE axis rotation and meters -> 100-units
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

	// Same native -> UE axis rotation as FSWGIFFChunkReader::ReadQuatLE.
	FQuat Result(Z, X, Y, W);
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

	if (!QchnReader.CanRead(6)) return;

	auto DecodeSample = [&](int32 FrameIndex)
	{
		const uint32 Packed = QchnReader.ReadValueLE<uint32>();
		OutTrack.Keyframes.Add(FrameIndex, DecodeCompressedQuaternion(Packed, RawScaleX, RawScaleY, RawScaleZ));
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

	auto DecodeSample = [&](int32 FrameIndex)
	{
		const float W = QchnReader.ReadValueLE<float>();
		const float X = QchnReader.ReadValueLE<float>();
		const float Y = QchnReader.ReadValueLE<float>();
		const float Z = QchnReader.ReadValueLE<float>();
		// Same native -> UE axis rotation as ReadQuatLE.
		OutTrack.Keyframes.Add(FrameIndex, FQuat(Z, X, Y, W));
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
			Result.Add(FQuat(Z, X, Y, W));
		}
	}

	return Result;
}

TMap<int32, float> FSWGAnimationReader::DecodeChnlChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Chnl, bool bIsCompressed)
{
	TMap<int32, float> Result;
	FSWGIFFChunkReader Channel(Chnl, Reader);
	const int32 IndexBytes = bIsCompressed ? 2 : 4;
	if (!Channel.CanRead(IndexBytes)) return Result;
	const uint32 Count = bIsCompressed ? Channel.ReadValueLE<uint16>() : Channel.ReadValueLE<uint32>();
	if (Count > (uint32)(Channel.GetRemaining() / (IndexBytes + 4))) return Result;
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		const uint32 Frame = bIsCompressed ? Channel.ReadValueLE<uint16>() : Channel.ReadValueLE<uint32>();
		const float Value = Channel.ReadValueLE<float>();
		if (Frame <= MAX_int32 && FMath::IsFinite(Value)) Result.Add((int32)Frame, Value);
	}
	return Result;
}

void FSWGAnimationReader::DecodeRootTranslation(const FSWGIffReader& Reader, const FSWGIffChunk& InnerForm, FSWGAnimationData& OutAnimation)
{
	FSWGIffChunk Loct;
	if (Reader.FindChildChunk(InnerForm, SWG_IFF_TAG('L','O','C','T'), Loct))
	{
		FSWGIFFChunkReader DistanceReader(Loct, Reader);
		float Distance = 0.f;
		if (DistanceReader.ReadValueLE(Distance) && FMath::IsFinite(Distance))
		{
			OutAnimation.RootTravelDistance = FMath::Abs(Distance) * SWGWorldScale;
		}
	}
	for (const FSWGAnimationBoneTrack& Track : OutAnimation.BoneTracks)
	{
		if (!Track.BoneName.Equals(TEXT("root"), ESearchCase::IgnoreCase)) continue;
		for (const auto& Key : Track.TranslationDeltas)
		{
			// The actor drives locomotion; retain vertical bob without sliding at loop seams.
			OutAnimation.RootTranslationDeltas.Add(Key.Key, FVector(0, 0, Key.Value.Z));
		}
	}
}

void FSWGAnimationReader::DecodeEvents(const FSWGIffReader& Reader, const FSWGIffChunk& InnerForm, FSWGAnimationData& OutAnimation)
{
	FSWGIffChunk MsgsForm;
	if (!Reader.FindChildForm(InnerForm, SWG_IFF_TAG('M','S','G','S'), MsgsForm))
	{
		return;
	}

	int32 DeclaredCount = 0;
	FSWGIffChunk InfoChunk;
	if (Reader.FindChildChunk(MsgsForm, SWGIffTags::Info, InfoChunk))
	{
		FSWGIFFChunkReader InfoReader(InfoChunk, Reader);
		DeclaredCount = InfoReader.ReadValueLE<uint16>();
	}

	for (const FSWGIffChunk& Mesg : Reader.FindAllChildChunks(MsgsForm, SWG_IFF_TAG('M','E','S','G')))
	{
		FSWGIFFChunkReader MesgReader(Mesg, Reader);
		const int32 FrameCount = MesgReader.ReadValueLE<uint16>();

		FSWGAnimationEvent Event;
		Event.Name = MesgReader.ReadTerminiatedString();
		if (Event.Name.IsEmpty() || !MesgReader.CanRead(FrameCount * 2))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationReader: malformed MESG '%s' (%d frame(s) declared), skipped"), *Event.Name, FrameCount);
			continue;
		}

		Event.Frames.Reserve(FrameCount);
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			Event.Frames.Add(MesgReader.ReadValueLE<uint16>());
		}
		OutAnimation.Events.Add(MoveTemp(Event));
	}

	if (OutAnimation.Events.Num() != DeclaredCount)
	{
		UE_LOG(LogTemp, Verbose, TEXT("FSWGAnimationReader: MSGS INFO declares %d message(s), decoded %d"), DeclaredCount, OutAnimation.Events.Num());
	}
}

bool FSWGAnimationReader::ReadAnimation(const FSWGIffReader& Reader, FSWGAnimationData& OutAnimation)
{
	OutAnimation = FSWGAnimationData();
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
	// SROT sits beside FORM AROT under the version form, not inside it — the
	// file order is XFRM, AROT, SROT, ATRN, STRN, all siblings. Searching AROT
	// for it silently yields nothing, leaving every hasTrack=0 bone on its bind
	// rotation; postures whose orientation is authored as a static rotation
	// rather than a keyframe track (prone stores the body's whole lie-down that
	// way) then play with the limbs posed but the body upright.
	const bool bHasSrot =
		Reader.FindChildChunk(InnerForm, SWG_IFF_TAG('S','R','O','T'), SrotChunk);

	const TArray<FQuat> StaticRotations = bHasSrot
		? DecodeStaticRotations(Reader, SrotChunk, bIsCompressed)
		: TArray<FQuat>();
	if (QchnChunks.Num() != AnimatedBoneCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGAnimationReader: expected %d animated bone(s) per INFO, found %d QCHN chunk(s) — bone/track mapping may be wrong"),
			AnimatedBoneCount, QchnChunks.Num());
	}

	TArray<TMap<int32, float>> TranslationChannels;
	TArray<float> StaticTranslations;
	FSWGIffChunk Atrn, Strn;
	if (Reader.FindChildForm(InnerForm, SWG_IFF_TAG('A','T','R','N'), Atrn))
	{
		for (const FSWGIffChunk& Channel : Reader.FindAllChildChunks(Atrn, SWG_IFF_TAG('C','H','N','L')))
		{
			TranslationChannels.Add(DecodeChnlChunk(Reader, Channel, bIsCompressed));
		}
	}
	if (Reader.FindChildChunk(InnerForm, SWG_IFF_TAG('S','T','R','N'), Strn))
	{
		FSWGIFFChunkReader Values(Strn, Reader);
		while (Values.CanRead(4)) StaticTranslations.Add(Values.ReadValueLE<float>());
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
		if (!XfinReader.AtEnd())
		{
			if (!XfinReader.CanRead(bIsCompressed ? 7 : 16)) return false;
			const uint32 TranslationMask = bIsCompressed ? XfinReader.ReadValueLE<uint8>() : XfinReader.ReadValueLE<uint32>();
			TMap<int32, float> Axes[3];
			TArray<int32> Frames[3];
			TSet<int32> AllFrames;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const uint32 Channel = bIsCompressed ? XfinReader.ReadValueLE<uint16>() : XfinReader.ReadValueLE<uint32>();
				if ((TranslationMask & (8u << Axis)) != 0)
				{
					if (Channel >= (uint32)TranslationChannels.Num() || TranslationChannels[Channel].IsEmpty()) return false;
					Axes[Axis] = TranslationChannels[Channel];
				}
				else
				{
					if (Channel >= (uint32)StaticTranslations.Num() || !FMath::IsFinite(StaticTranslations[Channel])) return false;
					Axes[Axis].Add(0, StaticTranslations[Channel]);
				}
				Axes[Axis].GetKeys(Frames[Axis]);
				Frames[Axis].Sort();
				for (int32 Frame : Frames[Axis]) if (Frame >= 0 && Frame < OutAnimation.FrameCount) AllFrames.Add(Frame);
			}
			auto Sample = [](const TMap<int32, float>& Values, const TArray<int32>& Keys, int32 Frame)
			{
				int32 Before = Keys[0];
				for (int32 After : Keys)
				{
					if (After >= Frame)
					{
						const float Alpha = After == Before ? 0.f : FMath::Clamp((float)(Frame - Before) / (After - Before), 0.f, 1.f);
						return FMath::Lerp(Values[Before], Values[After], Alpha);
					}
					Before = After;
				}
				return Values[Before];
			};
			for (int32 Frame : AllFrames)
			{
				Track.TranslationDeltas.Add(Frame, FVector(Sample(Axes[2], Frames[2], Frame), Sample(Axes[0], Frames[0], Frame), Sample(Axes[1], Frames[1], Frame)) * SWGWorldScale);
			}
		}

		if (bHasTrack)
		{
			if (!QchnChunks.IsValidIndex(RotationChannelIndex))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("FSWGAnimationReader: rotation channel %d for bone '%s' is outside %d QCHN chunk(s)"),
					RotationChannelIndex, *BoneName, QchnChunks.Num());
				if (!Track.TranslationDeltas.IsEmpty()) OutAnimation.BoneTracks.Add(MoveTemp(Track));
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
			if (Track.TranslationDeltas.IsEmpty()) continue;
		}

		OutAnimation.BoneTracks.Add(MoveTemp(Track));
	}

	DecodeRootTranslation(Reader, InnerForm, OutAnimation);
	DecodeEvents(Reader, InnerForm, OutAnimation);

	return OutAnimation.BoneTracks.Num() > 0;
}
