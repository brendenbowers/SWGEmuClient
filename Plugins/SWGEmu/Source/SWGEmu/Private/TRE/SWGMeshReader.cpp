#include "TRE/SWGMeshReader.h"

namespace
{
	float MeshReadFloatLE(const uint8* Data, int32 Offset)
	{
		float Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(float));
		return Value;
	}

	uint32 MeshReadUInt32LE(const uint8* Data, int32 Offset)
	{
		uint32 Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(uint32));
		return Value;
	}

	uint16 ReadUInt16LE(const uint8* Data, int32 Offset)
	{
		uint16 Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(uint16));
		return Value;
	}

	FVector ReadVector3LE(const uint8* Data, int32 Offset)
	{
		// SWG's mesh data is authored Y-up (model space); UE is Z-up. Reading
		// the file's (x,y,z) straight into UE's (X,Y,Z) with no conversion put
		// every creature's mesh (which stands upright in its own Y-up model
		// space) tipped 90 degrees in UE's Z-up world — reported as creatures
		// lying flat on their backs. Swapping Y/Z maps Y-up model space onto
		// Z-up world space directly. Applies equally to positions, normals, and
		// the bounding box (TryReadBoundingBox), all of which share this helper
		// and all need the same conversion.
		return FVector(MeshReadFloatLE(Data, Offset), MeshReadFloatLE(Data, Offset + 8), MeshReadFloatLE(Data, Offset + 4));
	}

	// Re-enabled (see chat): mesh geometry scales up to human-scale UE units
	// again. This time the rendered footprint (terrain tile count, world-
	// snapshot spawn radius) is being cut down alongside it, rather than
	// leaving those at their old "cover a huge small-scale area" settings,
	// which would now try to bake/spawn ~100x more effective world area.
	// Applies to position data only (never normals, which must stay
	// unit-length, or UVs).
	constexpr float MetersToWorldUnits = 100.0f;

	FVector ReadPositionVector3LE(const uint8* Data, int32 Offset)
	{
		return ReadVector3LE(Data, Offset) * MetersToWorldUnits;
	}
}

bool FSWGMeshReader::FindChildForm(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& FormType, FSWGIffChunk& OutChunk)
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

bool FSWGMeshReader::FindChildChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& Tag, FSWGIffChunk& OutChunk)
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

TArray<FSWGIffChunk> FSWGMeshReader::FindChildForms(const FSWGIffReader& Reader, const FSWGIffChunk& Parent)
{
	TArray<FSWGIffChunk> Result;
	for (const FSWGIffChunk& Child : Reader.ReadChildren(Parent))
	{
		if (Child.IsForm())
			Result.Add(Child);
	}
	return Result;
}

FString FSWGMeshReader::ReadNullTerminatedString(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk)
{
	const uint8* Data = Reader.GetChunkData(Chunk);
	// DataSize includes the trailing null the file stores after the string.
	const int32 Len = FMath::Max(0, Chunk.DataSize - 1);
	return FString::ConstructFromPtrSize((const ANSICHAR*)Data, Len);
}

TArray<FString> FSWGMeshReader::ReadAllNullTerminatedStrings(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk)
{
	TArray<FString> Names;
	const uint8* Data = Reader.GetChunkData(Chunk);
	int32 Start = 0;
	while (Start < Chunk.DataSize)
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

TArray<TArray<FSWGBoneWeight>> FSWGMeshReader::ReadVertexWeights(const FSWGIffReader& Reader, const FSWGIffChunk& Form0004, int32 VertexCount)
{
	TArray<TArray<FSWGBoneWeight>> VertexWeights;
	VertexWeights.SetNum(VertexCount);

	FSWGIffChunk TwhdChunk, TwdtChunk;
	if (!FindChildChunk(Reader, Form0004, TEXT("TWHD"), TwhdChunk) || !FindChildChunk(Reader, Form0004, TEXT("TWDT"), TwdtChunk))
	{
		return VertexWeights; // no weights present — bind-pose-only rendering doesn't need them
	}
	if (TwhdChunk.DataSize != VertexCount * 4)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: TWHD size %d doesn't match VertexCount %d (expected %d) — skipping weights"),
			TwhdChunk.DataSize, VertexCount, VertexCount * 4);
		return VertexWeights;
	}

	const uint8* TwhdData = Reader.GetChunkData(TwhdChunk);
	const uint8* TwdtData = Reader.GetChunkData(TwdtChunk);
	const int32 TwdtEntryCount = TwdtChunk.DataSize / 8; // {uint32 BoneIndex; float Weight} per entry

	int32 EntryCursor = 0;
	for (int32 v = 0; v < VertexCount; ++v)
	{
		const int32 InfluenceCount = (int32)MeshReadUInt32LE(TwhdData, v * 4);
		VertexWeights[v].Reserve(InfluenceCount);
		for (int32 i = 0; i < InfluenceCount; ++i)
		{
			if (EntryCursor >= TwdtEntryCount)
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: TWDT ran out of entries at vertex %d (expected %d total, TWDT only has %d) — weights truncated"),
					v, TwhdChunk.DataSize / 4, TwdtEntryCount);
				return VertexWeights;
			}
			FSWGBoneWeight Weight;
			Weight.BoneIndex = (int32)MeshReadUInt32LE(TwdtData, EntryCursor * 8);
			Weight.Weight = MeshReadFloatLE(TwdtData, EntryCursor * 8 + 4);
			VertexWeights[v].Add(Weight);
			++EntryCursor;
		}
	}
	return VertexWeights;
}

void FSWGMeshReader::TryReadBoundingBox(const FSWGIffReader& Reader, const FSWGIffChunk& Form0004, FSWGMeshData& OutMesh)
{
	FSWGIffChunk Appr, F0003, Exbx, Exbx0001, BoxChunk;
	if (!FindChildForm(Reader, Form0004, TEXT("APPR"), Appr)) return;
	if (!FindChildForm(Reader, Appr, TEXT("0003"), F0003)) return;
	if (!FindChildForm(Reader, F0003, TEXT("EXBX"), Exbx)) return;
	if (!FindChildForm(Reader, Exbx, TEXT("0001"), Exbx0001)) return;
	if (!FindChildChunk(Reader, Exbx0001, TEXT("BOX "), BoxChunk)) return;
	if (BoxChunk.DataSize < 24) return;

	const uint8* Data = Reader.GetChunkData(BoxChunk);
	const FVector A = ReadPositionVector3LE(Data, 0);
	const FVector B = ReadPositionVector3LE(Data, 12);

	FBox Box(ForceInit);
	Box += A;
	Box += B;
	OutMesh.BoundingBox = Box;
	OutMesh.bHasBoundingBox = true;
}

bool FSWGMeshReader::ReadMshSubmesh(const FSWGIffReader& Reader, const FSWGIffChunk& SubmeshForm, FSWGMeshSubmesh& OutSubmesh)
{
	FSWGIffChunk NameChunk;
	if (FindChildChunk(Reader, SubmeshForm, TEXT("NAME"), NameChunk))
	{
		OutSubmesh.ShaderName = ReadNullTerminatedString(Reader, NameChunk);
	}

	// SubmeshForm (e.g. FORM "0001") wraps: NAME, an outer INFO chunk, and a
	// nested FORM "0001" that itself holds an inner INFO chunk, FORM VTXA
	// (-> FORM 0003 -> INFO/DATA), and INDX — confirmed via a live chunk-tree
	// dump against a real .msh (thm_all_elevator_panel_down_s02_l0.msh).
	// INDX sits under the *inner* wrapper form, not directly under
	// SubmeshForm — that mismatch was why every submesh failed to parse.
	FSWGIffChunk Wrapper0001, Vtxa, F0003, InfoChunk, DataChunk, IndxChunk;
	if (!FindChildForm(Reader, SubmeshForm, TEXT("0001"), Wrapper0001)) return false;
	if (!FindChildForm(Reader, Wrapper0001, TEXT("VTXA"), Vtxa)) return false;
	if (!FindChildForm(Reader, Vtxa, TEXT("0003"), F0003)) return false;
	if (!FindChildChunk(Reader, F0003, TEXT("INFO"), InfoChunk)) return false;
	if (!FindChildChunk(Reader, F0003, TEXT("DATA"), DataChunk)) return false;
	if (!FindChildChunk(Reader, Wrapper0001, TEXT("INDX"), IndxChunk)) return false;

	const uint8* InfoData = Reader.GetChunkData(InfoChunk);
	const uint32 Flags = MeshReadUInt32LE(InfoData, 0);
	const uint32 VertexCount = MeshReadUInt32LE(InfoData, 4);
	if (VertexCount == 0) return false;

	const int32 Stride = DataChunk.DataSize / (int32)VertexCount;
	const bool bHasColor = (Flags & 0x08) != 0;
	const int32 ColorBytes = bHasColor ? 4 : 0;
	const int32 UvChannels = FMath::Max(0, (Stride - 24 - ColorBytes) / 8);

	const uint8* VertexData = Reader.GetChunkData(DataChunk);
	OutSubmesh.Vertices.Reserve((int32)VertexCount);
	for (uint32 v = 0; v < VertexCount; ++v)
	{
		const int32 Base = (int32)v * Stride;

		FSWGMeshVertex Vertex;
		Vertex.Position = ReadPositionVector3LE(VertexData, Base + 0);
		Vertex.Normal = ReadVector3LE(VertexData, Base + 12);

		int32 UvStart = Base + 24;
		if (bHasColor)
		{
			// Packed RGBA8. FColor's constructor takes (R,G,B,A) in that byte order.
			Vertex.Color = FColor(VertexData[UvStart], VertexData[UvStart + 1], VertexData[UvStart + 2], VertexData[UvStart + 3]);
			Vertex.bHasColor = true;
			UvStart += 4;
		}

		Vertex.UVs.Reserve(UvChannels);
		for (int32 ch = 0; ch < UvChannels; ++ch)
		{
			const float U = MeshReadFloatLE(VertexData, UvStart + ch * 8);
			const float V = MeshReadFloatLE(VertexData, UvStart + ch * 8 + 4);
			Vertex.UVs.Add(FVector2D(U, V));
		}

		OutSubmesh.Vertices.Add(MoveTemp(Vertex));
	}

	const uint8* IndxData = Reader.GetChunkData(IndxChunk);
	const uint32 TriIndexCount = MeshReadUInt32LE(IndxData, 0);
	OutSubmesh.Triangles.Reserve((int32)TriIndexCount);
	for (uint32 i = 0; i < TriIndexCount; ++i)
	{
		OutSubmesh.Triangles.Add((int32)ReadUInt16LE(IndxData, 4 + (int32)i * 2));
	}

	return true;
}

bool FSWGMeshReader::ReadStaticMesh(const FSWGIffReader& Reader, FSWGMeshData& OutMesh)
{
	if (!Reader.IsValid()) return false;

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0 || !TopLevel[0].IsForm() || TopLevel[0].FormType != TEXT("MESH"))
		return false;

	const FSWGIffChunk& MeshForm = TopLevel[0];

	// FORM MESH wraps a single version-tagged form — hardcoded "0004" only
	// matched the original small test file; real production meshes use "0005"
	// (confirmed against appearance/mesh/wp_mle_axe_heavy_duty_l0.msh). Same
	// version-drift pattern already hit and fixed in FSWGTerrainReader's PTAT
	// tag — take whichever single one is present rather than hardcode it,
	// since nothing below this depends on which.
	const TArray<FSWGIffChunk> MeshVersionForms = FindChildForms(Reader, MeshForm);
	if (MeshVersionForms.Num() == 0) return false;
	const FSWGIffChunk& Form0004 = MeshVersionForms[0];

	TryReadBoundingBox(Reader, Form0004, OutMesh);

	FSWGIffChunk SpsForm, Sps0001;
	if (!FindChildForm(Reader, Form0004, TEXT("SPS "), SpsForm)) return false;
	if (!FindChildForm(Reader, SpsForm, TEXT("0001"), Sps0001)) return false;

	for (const FSWGIffChunk& SubmeshForm : FindChildForms(Reader, Sps0001))
	{
		FSWGMeshSubmesh Submesh;
		if (ReadMshSubmesh(Reader, SubmeshForm, Submesh))
		{
			OutMesh.Submeshes.Add(MoveTemp(Submesh));
		}
		else
		{
			// Previously silent — ReadStaticMesh still returns true as long as
			// at least one submesh parses, so a partial failure (most
			// submeshes dropped, one small one surviving) looked like success
			// while actually rendering only a fragment of the mesh.
			UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: failed to parse submesh FORM %s — skipping"), *SubmeshForm.FormType);
		}
	}

	return OutMesh.Submeshes.Num() > 0;
}

bool FSWGMeshReader::ReadMgnSubmesh(const FSWGIffReader& Reader, const FSWGIffChunk& PsdtForm, const TArray<FVector>& Positions, const TArray<FVector>& Normals,
	const TArray<TArray<FSWGBoneWeight>>& VertexWeights, FSWGMeshSubmesh& OutSubmesh)
{
	FSWGIffChunk NameChunk;
	if (FindChildChunk(Reader, PsdtForm, TEXT("NAME"), NameChunk))
	{
		OutSubmesh.ShaderName = ReadNullTerminatedString(Reader, NameChunk);
	}

	FSWGIffChunk PidxChunk, NidxChunk, TcsfForm, TcsdChunk, PrimForm, OitlChunk;
	if (!FindChildChunk(Reader, PsdtForm, TEXT("PIDX"), PidxChunk))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s missing PIDX"), *PsdtForm.FormType);
		return false;
	}
	if (!FindChildChunk(Reader, PsdtForm, TEXT("NIDX"), NidxChunk))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s missing NIDX"), *PsdtForm.FormType);
		return false;
	}
	if (!FindChildForm(Reader, PsdtForm, TEXT("TCSF"), TcsfForm))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s missing TCSF"), *PsdtForm.FormType);
		return false;
	}
	if (!FindChildChunk(Reader, TcsfForm, TEXT("TCSD"), TcsdChunk))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s TCSF missing TCSD"), *PsdtForm.FormType);
		return false;
	}
	if (!FindChildForm(Reader, PsdtForm, TEXT("PRIM"), PrimForm))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s missing PRIM"), *PsdtForm.FormType);
		return false;
	}
	// Unlike FORM MESH's static submeshes (which use a plain "ITL " tag with
	// 12-byte/no-flag records — see ReadMshSubmesh), a .mgn PSDT's PRIM form
	// genuinely uses "OITL" — confirmed via a live PRIM child dump (only
	// children were INFO and OITL, no "ITL " at all). These are two distinct
	// on-disk conventions, not the same bug in two places.
	if (!FindChildChunk(Reader, PrimForm, TEXT("OITL"), OitlChunk))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s PRIM missing OITL"), *PsdtForm.FormType);
		return false;
	}

	// PIDX carries its own leading count prefix; NIDX/TCSD do not (their counts
	// are implied to match PIDX's) — confirmed against real samples, see
	// world-object-plan.html "Parsing gotcha hit while decoding this".
	const uint8* PidxData = Reader.GetChunkData(PidxChunk);
	const uint32 CornerCount = MeshReadUInt32LE(PidxData, 0);
	if (CornerCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s has zero CornerCount"), *PsdtForm.FormType);
		return false;
	}

	const uint8* NidxData = Reader.GetChunkData(NidxChunk);
	const uint8* TcsdData = Reader.GetChunkData(TcsdChunk);

	OutSubmesh.Vertices.Reserve((int32)CornerCount);
	for (uint32 c = 0; c < CornerCount; ++c)
	{
		const uint32 PosIndex = MeshReadUInt32LE(PidxData, 4 + (int32)c * 4);
		const uint32 NormIndex = MeshReadUInt32LE(NidxData, (int32)c * 4);
		if ((int32)PosIndex >= Positions.Num() || (int32)NormIndex >= Normals.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s corner %d out of range (PosIndex=%d/%d, NormIndex=%d/%d)"),
				*PsdtForm.FormType, c, PosIndex, Positions.Num(), NormIndex, Normals.Num());
			return false;
		}

		FSWGMeshVertex Vertex;
		Vertex.Position = Positions[PosIndex];
		Vertex.Normal = Normals[NormIndex];
		Vertex.UVs.Add(FVector2D(MeshReadFloatLE(TcsdData, (int32)c * 8), MeshReadFloatLE(TcsdData, (int32)c * 8 + 4)));
		// Skin weights are indexed by POSN index too (same as Position above),
		// not by corner index — see world-object-plan.html's TWHD/TWDT entry.
		if (VertexWeights.IsValidIndex((int32)PosIndex))
		{
			Vertex.BoneWeights = VertexWeights[PosIndex];
		}
		OutSubmesh.Vertices.Add(MoveTemp(Vertex));
	}

	// OITL record = [uint16 flag][uint32 corner0][uint32 corner1][uint32 corner2]
	// = 14 bytes — confirmed against a real chunk (DataSize=2160: (2160-4)/14
	// = 154 exactly, whereas /12 isn't an integer). This is the format the
	// static-mesh "ITL " fix moved away from — that fix was specific to FORM
	// MESH's plain "ITL " tag, not this one.
	const uint8* OitlData = Reader.GetChunkData(OitlChunk);
	const uint32 TriCount = MeshReadUInt32LE(OitlData, 0);
	constexpr int32 RecordBytes = 14;
	OutSubmesh.Triangles.Reserve((int32)TriCount * 3);
	for (uint32 t = 0; t < TriCount; ++t)
	{
		const int32 RecordOffset = 4 + (int32)t * RecordBytes + 2; // +2 skips the leading uint16 flag
		OutSubmesh.Triangles.Add((int32)MeshReadUInt32LE(OitlData, RecordOffset + 0));
		OutSubmesh.Triangles.Add((int32)MeshReadUInt32LE(OitlData, RecordOffset + 4));
		OutSubmesh.Triangles.Add((int32)MeshReadUInt32LE(OitlData, RecordOffset + 8));
	}

	return true;
}

bool FSWGMeshReader::ReadSkeletalMeshBindPose(const FSWGIffReader& Reader, FSWGMeshData& OutMesh)
{
	if (!Reader.IsValid()) return false;

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0 || !TopLevel[0].IsForm() || TopLevel[0].FormType != TEXT("SKMG"))
		return false;

	const FSWGIffChunk& SkmgForm = TopLevel[0];

	// Same version-tag drift as FORM MESH's inner form (see ReadStaticMesh) —
	// hardcoded "0004" doesn't match every real .mgn; take whichever single
	// version-tagged form is actually present.
	const TArray<FSWGIffChunk> SkmgVersionForms = FindChildForms(Reader, SkmgForm);
	if (SkmgVersionForms.Num() == 0) return false;
	const FSWGIffChunk& Form0004 = SkmgVersionForms[0];

	FSWGIffChunk PosnChunk, NormChunk;
	if (!FindChildChunk(Reader, Form0004, TEXT("POSN"), PosnChunk)) return false;
	if (!FindChildChunk(Reader, Form0004, TEXT("NORM"), NormChunk)) return false;

	const uint8* PosnData = Reader.GetChunkData(PosnChunk);
	const uint8* NormData = Reader.GetChunkData(NormChunk);
	const int32 VertexCount = PosnChunk.DataSize / 12;

	// See ReadPositionVector3LE's comment — .mgn position data is in meters,
	// same as .msh, and needs the same scale-up to match the rest of the
	// codebase's world-unit convention.
	TArray<FVector> Positions, Normals;
	Positions.Reserve(VertexCount);
	Normals.Reserve(VertexCount);
	for (int32 i = 0; i < VertexCount; ++i)
	{
		Positions.Add(ReadPositionVector3LE(PosnData, i * 12));
		Normals.Add(ReadVector3LE(NormData, i * 12));
	}

	// XFNM is this mesh's own bone name list — FSWGBoneWeight::BoneIndex
	// (from TWDT below) indexes into it, not into a skeleton's joint array
	// directly (see the header's BoneNames comment).
	FSWGIffChunk XfnmChunk;
	if (FindChildChunk(Reader, Form0004, TEXT("XFNM"), XfnmChunk))
	{
		OutMesh.BoneNames = ReadAllNullTerminatedStrings(Reader, XfnmChunk);
	}

	const TArray<TArray<FSWGBoneWeight>> VertexWeights = ReadVertexWeights(Reader, Form0004, VertexCount);

	int32 PsdtCount = 0;
	for (const FSWGIffChunk& Child : FindChildForms(Reader, Form0004))
	{
		if (Child.FormType != TEXT("PSDT")) continue;
		++PsdtCount;

		FSWGMeshSubmesh Submesh;
		if (ReadMgnSubmesh(Reader, Child, Positions, Normals, VertexWeights, Submesh))
		{
			OutMesh.Submeshes.Add(MoveTemp(Submesh));
		}
	}

	if (PsdtCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: FORM %s (SKMG version form) has no PSDT children"), *Form0004.FormType);
	}

	return OutMesh.Submeshes.Num() > 0;
}
