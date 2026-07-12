#include "TRE/SWGSkeletonReader.h"

namespace
{
	float SkeletonReadFloatLE(const uint8* Data, int32 Offset)
	{
		float Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(float));
		return Value;
	}

	uint32 SkeletonReadUInt32LE(const uint8* Data, int32 Offset)
	{
		uint32 Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(uint32));
		return Value;
	}

	int32 SkeletonReadInt32LE(const uint8* Data, int32 Offset)
	{
		int32 Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(int32));
		return Value;
	}

	// Same Y-up (model space) -> Z-up (UE) axis swap and meters -> 100-units
	// scale as SWGMeshReader.cpp's ReadPositionVector3LE — bind pose
	// translations are authored in the same convention as mesh positions
	// (confirmed empirically: the root joint's raw Y value is ~1.0, a
	// plausible hip height in meters, not raw UE units).
	constexpr float MetersToWorldUnits = 100.0f;

	FVector SkeletonReadTranslationLE(const uint8* Data, int32 Offset)
	{
		const float X = SkeletonReadFloatLE(Data, Offset);
		const float Y = SkeletonReadFloatLE(Data, Offset + 4);
		const float Z = SkeletonReadFloatLE(Data, Offset + 8);
		return FVector(X, Z, Y) * MetersToWorldUnits;
	}

	// Quaternions are stored (W,X,Y,Z) — confirmed against the root joint's
	// BPRO/RPRE, which is (1,0,0,0) on disk; that's only the identity
	// quaternion if the first float is W, not X. Applies the same Y/Z swap
	// as SkeletonReadTranslationLE, on the vector (X,Y,Z) part only.
	FQuat SkeletonReadRotationLE(const uint8* Data, int32 Offset)
	{
		const float W = SkeletonReadFloatLE(Data, Offset);
		const float X = SkeletonReadFloatLE(Data, Offset + 4);
		const float Y = SkeletonReadFloatLE(Data, Offset + 8);
		const float Z = SkeletonReadFloatLE(Data, Offset + 12);
		return FQuat(X, Z, Y, W);
	}
}

bool FSWGSkeletonReader::FindChildForm(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& FormType, FSWGIffChunk& OutChunk)
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

bool FSWGSkeletonReader::FindChildChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& Tag, FSWGIffChunk& OutChunk)
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

TArray<FSWGIffChunk> FSWGSkeletonReader::FindChildForms(const FSWGIffReader& Reader, const FSWGIffChunk& Parent)
{
	TArray<FSWGIffChunk> Result;
	for (const FSWGIffChunk& Child : Reader.ReadChildren(Parent))
	{
		if (Child.IsForm())
			Result.Add(Child);
	}
	return Result;
}

TArray<FString> FSWGSkeletonReader::ReadNullTerminatedStrings(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk, int32 Count)
{
	TArray<FString> Names;
	Names.Reserve(Count);

	const uint8* Data = Reader.GetChunkData(Chunk);
	int32 Start = 0;
	for (int32 i = 0; i < Count && Start < Chunk.DataSize; ++i)
	{
		int32 End = Start;
		while (End < Chunk.DataSize && Data[End] != 0)
		{
			++End;
		}
		Names.Add(FString::ConstructFromPtrSize((const ANSICHAR*)(Data + Start), End - Start));
		Start = End + 1; // skip the null terminator
	}
	return Names;
}

bool FSWGSkeletonReader::ReadSkeleton(const FSWGIffReader& Reader, FSWGSkeletonData& OutSkeleton)
{
	if (!Reader.IsValid()) return false;

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0 || !TopLevel[0].IsForm() || TopLevel[0].FormType != TEXT("SLOD"))
		return false;

	const FSWGIffChunk& SlodForm = TopLevel[0];

	// Same version-tag-drift pattern as FSWGMeshReader's FORM MESH/SKMG outer
	// wrapper — take whichever single version-tagged form is present rather
	// than hardcode "0000".
	const TArray<FSWGIffChunk> SlodVersionForms = FindChildForms(Reader, SlodForm);
	if (SlodVersionForms.Num() == 0) return false;
	const FSWGIffChunk& Form0000 = SlodVersionForms[0];

	// FORM SLOD > FORM 0000 wraps one FORM SKTM per skeleton LOD level, most
	// detailed (highest joint count) first — only that first one is used;
	// there's no current need for the coarser LODs the game uses for distant
	// creatures.
	FSWGIffChunk SktmForm;
	if (!FindChildForm(Reader, Form0000, TEXT("SKTM"), SktmForm)) return false;

	// FORM SKTM wraps a single version-tagged form too, same pattern again.
	const TArray<FSWGIffChunk> SktmVersionForms = FindChildForms(Reader, SktmForm);
	if (SktmVersionForms.Num() == 0) return false;
	const FSWGIffChunk& SktmInner = SktmVersionForms[0];

	FSWGIffChunk InfoChunk, NameChunk, PrntChunk, BptrChunk, BproChunk;
	if (!FindChildChunk(Reader, SktmInner, TEXT("INFO"), InfoChunk)) return false;
	if (!FindChildChunk(Reader, SktmInner, TEXT("NAME"), NameChunk)) return false;
	if (!FindChildChunk(Reader, SktmInner, TEXT("PRNT"), PrntChunk)) return false;
	if (!FindChildChunk(Reader, SktmInner, TEXT("BPTR"), BptrChunk)) return false;
	if (!FindChildChunk(Reader, SktmInner, TEXT("BPRO"), BproChunk)) return false;

	// RPRE/RPST (per-joint pre/post rotation quaternions) are optional-safe:
	// missing chunks leave every joint at identity, which degrades to the old
	// (pre-fix) behavior instead of failing the whole skeleton.
	FSWGIffChunk RpreChunk, RpstChunk;
	const bool bHasRpre = FindChildChunk(Reader, SktmInner, TEXT("RPRE"), RpreChunk);
	const bool bHasRpst = FindChildChunk(Reader, SktmInner, TEXT("RPST"), RpstChunk);

	const uint8* InfoData = Reader.GetChunkData(InfoChunk);
	const int32 JointCount = (int32)SkeletonReadUInt32LE(InfoData, 0);
	if (JointCount <= 0) return false;

	const TArray<FString> Names = ReadNullTerminatedStrings(Reader, NameChunk, JointCount);
	if (Names.Num() != JointCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletonReader: expected %d joint names, NAME chunk yielded %d"), JointCount, Names.Num());
		return false;
	}

	const uint8* PrntData = Reader.GetChunkData(PrntChunk);
	const uint8* BptrData = Reader.GetChunkData(BptrChunk);
	const uint8* BproData = Reader.GetChunkData(BproChunk);
	const uint8* RpreData = bHasRpre ? Reader.GetChunkData(RpreChunk) : nullptr;
	const uint8* RpstData = bHasRpst ? Reader.GetChunkData(RpstChunk) : nullptr;

	OutSkeleton.Joints.Reserve(JointCount);
	for (int32 i = 0; i < JointCount; ++i)
	{
		FSWGSkeletonJoint Joint;
		Joint.Name = Names[i];
		Joint.ParentIndex = SkeletonReadInt32LE(PrntData, i * 4);
		Joint.BindPoseTranslation = SkeletonReadTranslationLE(BptrData, i * 12);
		Joint.BindPoseRotation = SkeletonReadRotationLE(BproData, i * 16);
		if (RpreData) Joint.PreRotation = SkeletonReadRotationLE(RpreData, i * 16);
		if (RpstData) Joint.PostRotation = SkeletonReadRotationLE(RpstData, i * 16);
		OutSkeleton.Joints.Add(MoveTemp(Joint));
	}

	return true;
}
