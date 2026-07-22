#include "TRE/SWGMeshReader.h"
#include "TRE/SWGIffTags.h"
#include "TRE/SWGIFFChunkReader.h"
#include "Common/SWGWorldScale.h"

FString FSWGMeshReader::ReadNullTerminatedString(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk)
{
	FSWGIFFChunkReader ChunkReader(Chunk, Reader);
	return ChunkReader.ReadTerminiatedString();
}

TArray<FString> FSWGMeshReader::ReadAllNullTerminatedStrings(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk)
{
	FSWGIFFChunkReader ChunkReader(Chunk, Reader);
	return ChunkReader.ReadTerminatedStrings(-1);
}

TArray<TArray<FSWGBoneWeight>> FSWGMeshReader::ReadVertexWeights(const FSWGIffReader& Reader, const FSWGIffChunk& Form0004, int32 VertexCount)
{
	TArray<TArray<FSWGBoneWeight>> VertexWeights;
	VertexWeights.SetNum(VertexCount);

	FSWGIffChunk TwhdChunk, TwdtChunk;
	if (!Reader.FindChildChunk(Form0004, SWG_IFF_TAG('T','W','H','D'), TwhdChunk) || !Reader.FindChildChunk(Form0004, SWG_IFF_TAG('T','W','D','T'), TwdtChunk))
	{
		return VertexWeights; // no weights present — bind-pose-only rendering doesn't need them
	}
	if (TwhdChunk.DataSize != VertexCount * 4)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: TWHD size %d doesn't match VertexCount %d (expected %d) — skipping weights"),
			TwhdChunk.DataSize, VertexCount, VertexCount * 4);
		return VertexWeights;
	}

	FSWGIFFChunkReader TwhdReader(TwhdChunk, Reader);
	FSWGIFFChunkReader TwdtReader(TwdtChunk, Reader);
	const int32 TwdtEntryCount = TwdtChunk.DataSize / 8; // {uint32 BoneIndex; float Weight} per entry

	int32 EntryCursor = 0;
	for (int32 v = 0; v < VertexCount; ++v)
	{
		const int32 InfluenceCount = (int32)TwhdReader.ReadValueLE<uint32>();
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
			Weight.BoneIndex = (int32)TwdtReader.ReadValueLE<uint32>();
			Weight.Weight = TwdtReader.ReadValueLE<float>();
			VertexWeights[v].Add(Weight);
			++EntryCursor;
		}
	}
	return VertexWeights;
}

void FSWGMeshReader::TryReadBoundingBox(const FSWGIffReader& Reader, const FSWGIffChunk& Form0004, FSWGMeshData& OutMesh)
{
	FSWGIffChunk Appr, F0003, Exbx, Exbx0001, BoxChunk;
	if (!Reader.FindChildForm(Form0004, SWG_IFF_TAG('A','P','P','R'), Appr)) return;
	if (!Reader.FindChildForm(Appr, SWG_IFF_TAG('0','0','0','3'), F0003)) return;
	if (!Reader.FindChildForm(F0003, SWG_IFF_TAG('E','X','B','X'), Exbx)) return;
	if (!Reader.FindChildForm(Exbx, SWG_IFF_TAG('0','0','0','1'), Exbx0001)) return;
	if (!Reader.FindChildChunk(Exbx0001, SWG_IFF_TAG('B','O','X',' '), BoxChunk)) return;
	if (BoxChunk.DataSize < 24) return;

	FSWGIFFChunkReader ChunkReader(BoxChunk, Reader);
	const FVector A = ChunkReader.ReadVectorLE<FVector, float>(SWGWorldScale);
	const FVector B = ChunkReader.ReadVectorLE<FVector, float>(SWGWorldScale);

	FBox Box(ForceInit);
	Box += A;
	Box += B;
	OutMesh.BoundingBox = Box;
	OutMesh.bHasBoundingBox = true;
}

bool FSWGMeshReader::ReadMshSubmesh(const FSWGIffReader& Reader, const FSWGIffChunk& SubmeshForm, FSWGMeshSubmesh& OutSubmesh)
{
	FSWGIffChunk NameChunk;
	if (Reader.FindChildChunk(SubmeshForm, SWGIffTags::Name, NameChunk))
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
	if (!Reader.FindChildForm(SubmeshForm, SWG_IFF_TAG('0','0','0','1'), Wrapper0001)) return false;
	if (!Reader.FindChildForm(Wrapper0001, SWG_IFF_TAG('V','T','X','A'), Vtxa)) return false;
	if (!Reader.FindChildForm(Vtxa, SWG_IFF_TAG('0','0','0','3'), F0003)) return false;
	if (!Reader.FindChildChunk(F0003, SWGIffTags::Info, InfoChunk)) return false;
	if (!Reader.FindChildChunk(F0003, SWGIffTags::Data, DataChunk)) return false;
	if (!Reader.FindChildChunk(Wrapper0001, SWG_IFF_TAG('I','N','D','X'), IndxChunk)) return false;

	FSWGIFFChunkReader InfoReader(InfoChunk, Reader);
	const uint32 Flags = InfoReader.ReadValueLE<uint32>();
	const uint32 VertexCount = InfoReader.ReadValueLE<uint32>();
	if (VertexCount == 0) return false;

	const int32 Stride = DataChunk.DataSize / (int32)VertexCount;
	const bool bHasColor = (Flags & 0x08) != 0;
	const int32 ColorBytes = bHasColor ? 4 : 0;
	const int32 UvChannels = FMath::Max(0, (Stride - 24 - ColorBytes) / 8);

	FSWGIFFChunkReader VertexReader(DataChunk, Reader);
	OutSubmesh.Vertices.Reserve((int32)VertexCount);
	for (uint32 v = 0; v < VertexCount; ++v)
	{
		FSWGMeshVertex Vertex;
		Vertex.Position = VertexReader.ReadVectorLE<FVector, float>(SWGWorldScale);
		Vertex.Normal = VertexReader.ReadVectorLE<FVector, float>(1.0f);

		if (bHasColor)
		{
			// Packed RGBA8. FColor's constructor takes (R,G,B,A) in that byte order.
			const uint8 R = VertexReader.ReadValueLE<uint8>();
			const uint8 G = VertexReader.ReadValueLE<uint8>();
			const uint8 B = VertexReader.ReadValueLE<uint8>();
			const uint8 A = VertexReader.ReadValueLE<uint8>();
			Vertex.Color = FColor(R, G, B, A);
			Vertex.bHasColor = true;
		}

		Vertex.UVs.Reserve(UvChannels);
		for (int32 ch = 0; ch < UvChannels; ++ch)
		{
			const float U = VertexReader.ReadValueLE<float>();
			const float V = VertexReader.ReadValueLE<float>();
			// The reference exporter writes 1-V into FBX, and Unreal's FBX
			// importer flips it again while creating wedges. This direct path
			// bypasses FBX, so retain the raw SWG V value here.
			Vertex.UVs.Add(FVector2D(U, V));
		}

		OutSubmesh.Vertices.Add(MoveTemp(Vertex));
	}

	FSWGIFFChunkReader IndxReader(IndxChunk, Reader);
	const uint32 TriIndexCount = IndxReader.ReadValueLE<uint32>();
	OutSubmesh.Triangles.Reserve((int32)TriIndexCount);
	for (uint32 i = 0; i < TriIndexCount; ++i)
	{
		OutSubmesh.Triangles.Add((int32)IndxReader.ReadValueLE<uint16>());
	}

	return true;
}

bool FSWGMeshReader::ReadStaticMesh(const FSWGIffReader& Reader, FSWGMeshData& OutMesh)
{
	if (!Reader.IsValid()) return false;

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0 || !TopLevel[0].IsForm() || TopLevel[0].FormType != SWG_IFF_TAG('M','E','S','H'))
		return false;

	const FSWGIffChunk& MeshForm = TopLevel[0];

	// FORM MESH wraps a single version-tagged form — hardcoded "0004" only
	// matched the original small test file; real production meshes use "0005"
	// (confirmed against appearance/mesh/wp_mle_axe_heavy_duty_l0.msh). Same
	// version-drift pattern already hit and fixed in FSWGTerrainReader's PTAT
	// tag — take whichever single one is present rather than hardcode it,
	// since nothing below this depends on which.
	const TArray<FSWGIffChunk> MeshVersionForms = Reader.FindChildForms(MeshForm);
	if (MeshVersionForms.Num() == 0) return false;
	const FSWGIffChunk& Form0004 = MeshVersionForms[0];

	TryReadBoundingBox(Reader, Form0004, OutMesh);

	FSWGIffChunk SpsForm, Sps0001;
	if (!Reader.FindChildForm(Form0004, SWG_IFF_TAG('S','P','S',' '), SpsForm)) return false;
	if (!Reader.FindChildForm(SpsForm, SWG_IFF_TAG('0','0','0','1'), Sps0001)) return false;

	for (const FSWGIffChunk& SubmeshForm : Reader.FindChildForms(Sps0001))
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
			UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: failed to parse submesh FORM %s — skipping"), *SubmeshForm.FormType.ToString());
		}
	}

	return OutMesh.Submeshes.Num() > 0;
}

bool FSWGMeshReader::ReadMgnSubmesh(const FSWGIffReader& Reader, const FSWGIffChunk& PsdtForm, const TArray<FVector>& Positions, const TArray<FVector>& Normals,
	const TArray<TArray<FSWGBoneWeight>>& VertexWeights, FSWGMeshSubmesh& OutSubmesh)
{
	FSWGIffChunk NameChunk;
	if (Reader.FindChildChunk(PsdtForm, SWGIffTags::Name, NameChunk))
	{
		OutSubmesh.ShaderName = ReadNullTerminatedString(Reader, NameChunk);
	}

	FSWGIffChunk PidxChunk, NidxChunk, TcsfForm, TcsdChunk, PrimForm, OitlChunk;
	if (!Reader.FindChildChunk(PsdtForm, SWG_IFF_TAG('P','I','D','X'), PidxChunk))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s missing PIDX"), *PsdtForm.FormType.ToString());
		return false;
	}
	if (!Reader.FindChildChunk(PsdtForm, SWG_IFF_TAG('N','I','D','X'), NidxChunk))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s missing NIDX"), *PsdtForm.FormType.ToString());
		return false;
	}
	if (!Reader.FindChildForm(PsdtForm, SWG_IFF_TAG('T','C','S','F'), TcsfForm))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s missing TCSF"), *PsdtForm.FormType.ToString());
		return false;
	}
	if (!Reader.FindChildChunk(TcsfForm, SWG_IFF_TAG('T','C','S','D'), TcsdChunk))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s TCSF missing TCSD"), *PsdtForm.FormType.ToString());
		return false;
	}
	if (!Reader.FindChildForm(PsdtForm, SWG_IFF_TAG('P','R','I','M'), PrimForm))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s missing PRIM"), *PsdtForm.FormType.ToString());
		return false;
	}
	// Unlike FORM MESH's static submeshes (which use a plain "ITL " tag with
	// 12-byte/no-flag records — see ReadMshSubmesh), a .mgn PSDT's PRIM form
	// genuinely uses "OITL" — confirmed via a live PRIM child dump (only
	// children were INFO and OITL, no "ITL " at all). These are two distinct
	// on-disk conventions, not the same bug in two places.
	if (!Reader.FindChildChunk(PrimForm, SWG_IFF_TAG('O','I','T','L'), OitlChunk))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s PRIM missing OITL"), *PsdtForm.FormType.ToString());
		return false;
	}

	// PIDX carries its own leading count prefix; NIDX/TCSD do not (their counts
	// are implied to match PIDX's) — confirmed against real samples, see
	// world-object-plan.html "Parsing gotcha hit while decoding this".
	FSWGIFFChunkReader PidxReader(PidxChunk, Reader);
	const uint32 CornerCount = PidxReader.ReadValueLE<uint32>();
	if (CornerCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s has zero CornerCount"), *PsdtForm.FormType.ToString());
		return false;
	}

	FSWGIFFChunkReader NidxReader(NidxChunk, Reader);
	FSWGIFFChunkReader TcsdReader(TcsdChunk, Reader);

	OutSubmesh.Vertices.Reserve((int32)CornerCount);
	for (uint32 c = 0; c < CornerCount; ++c)
	{
		const uint32 PosIndex = PidxReader.ReadValueLE<uint32>();
		const uint32 NormIndex = NidxReader.ReadValueLE<uint32>();
		if ((int32)PosIndex >= Positions.Num() || (int32)NormIndex >= Normals.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s corner %d out of range (PosIndex=%d/%d, NormIndex=%d/%d)"),
				*PsdtForm.FormType.ToString(), c, PosIndex, Positions.Num(), NormIndex, Normals.Num());
			return false;
		}

		FSWGMeshVertex Vertex;
		Vertex.Position = Positions[PosIndex];
		Vertex.Normal = Normals[NormIndex];
		const float U = TcsdReader.ReadValueLE<float>();
		const float V = TcsdReader.ReadValueLE<float>();
		Vertex.UVs.Add(FVector2D(U, V));
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
	FSWGIFFChunkReader OitlReader(OitlChunk, Reader);
	const uint32 TriCount = OitlReader.ReadValueLE<uint32>();
	OutSubmesh.Triangles.Reserve((int32)TriCount * 3);
	for (uint32 t = 0; t < TriCount; ++t)
	{
		OitlReader.Skip<uint16>(); // leading flag, unused
		OutSubmesh.Triangles.Add((int32)OitlReader.ReadValueLE<uint32>());
		OutSubmesh.Triangles.Add((int32)OitlReader.ReadValueLE<uint32>());
		OutSubmesh.Triangles.Add((int32)OitlReader.ReadValueLE<uint32>());
	}

	return true;
}

bool FSWGMeshReader::ReadSkeletalMeshBindPose(const FSWGIffReader& Reader, FSWGMeshData& OutMesh)
{
	if (!Reader.IsValid()) return false;

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0 || !TopLevel[0].IsForm() || TopLevel[0].FormType != SWG_IFF_TAG('S','K','M','G'))
		return false;

	const FSWGIffChunk& SkmgForm = TopLevel[0];

	// Same version-tag drift as FORM MESH's inner form (see ReadStaticMesh) —
	// hardcoded "0004" doesn't match every real .mgn; take whichever single
	// version-tagged form is actually present.
	const TArray<FSWGIffChunk> SkmgVersionForms = Reader.FindChildForms(SkmgForm);
	if (SkmgVersionForms.Num() == 0) return false;
	const FSWGIffChunk& Form0004 = SkmgVersionForms[0];

	FSWGIffChunk PosnChunk, NormChunk;
	if (!Reader.FindChildChunk(Form0004, SWG_IFF_TAG('P','O','S','N'), PosnChunk)) return false;
	if (!Reader.FindChildChunk(Form0004, SWG_IFF_TAG('N','O','R','M'), NormChunk)) return false;

	FSWGIFFChunkReader PosnReader(PosnChunk, Reader);
	FSWGIFFChunkReader NormReader(NormChunk, Reader);
	const int32 VertexCount = PosnChunk.DataSize / 12;

	// .mgn position data is in meters, same as .msh, and needs the same
	// scale-up to match the rest of the codebase's world-unit convention;
	// normals stay unscaled (unit-length).
	TArray<FVector> Positions, Normals;
	Positions.Reserve(VertexCount);
	Normals.Reserve(VertexCount);
	for (int32 i = 0; i < VertexCount; ++i)
	{
		Positions.Add(PosnReader.ReadVectorLE<FVector, float>(SWGWorldScale));
		Normals.Add(NormReader.ReadVectorLE<FVector, float>(1.0f));
	}

	// XFNM is this mesh's own bone name list — FSWGBoneWeight::BoneIndex
	// (from TWDT below) indexes into it, not into a skeleton's joint array
	// directly (see the header's BoneNames comment).
	FSWGIffChunk XfnmChunk;
	if (Reader.FindChildChunk(Form0004, SWG_IFF_TAG('X','F','N','M'), XfnmChunk))
	{
		OutMesh.BoneNames = ReadAllNullTerminatedStrings(Reader, XfnmChunk);
	}

	const TArray<TArray<FSWGBoneWeight>> VertexWeights = ReadVertexWeights(Reader, Form0004, VertexCount);

	int32 PsdtCount = 0;
	for (const FSWGIffChunk& Child : Reader.FindChildForms(Form0004))
	{
		if (Child.FormType != SWG_IFF_TAG('P','S','D','T')) continue;
		++PsdtCount;

		FSWGMeshSubmesh Submesh;
		if (ReadMgnSubmesh(Reader, Child, Positions, Normals, VertexWeights, Submesh))
		{
			OutMesh.Submeshes.Add(MoveTemp(Submesh));
		}
	}

	if (PsdtCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: FORM %s (SKMG version form) has no PSDT children"), *Form0004.FormType.ToString());
	}

	return OutMesh.Submeshes.Num() > 0;
}

FString FSWGMeshReader::ReadSkeletalMeshSkeletonPath(const FSWGIffReader& Reader)
{
	const TArray<FString> Paths = ReadSkeletalMeshSkeletonPaths(Reader);
	return Paths.IsEmpty() ? FString() : Paths[0];
}

TArray<FString> FSWGMeshReader::ReadSkeletalMeshSkeletonPaths(const FSWGIffReader& Reader)
{
	if (!Reader.IsValid()) return {};
	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0 || !TopLevel[0].IsForm() || TopLevel[0].FormType != SWG_IFF_TAG('S','K','M','G')) return {};

	const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(TopLevel[0]);
	if (VersionForms.Num() == 0) return {};

	FSWGIffChunk SktmChunk;
	return Reader.FindChildChunk(VersionForms[0], SWG_IFF_TAG('S','K','T','M'), SktmChunk)
		? ReadAllNullTerminatedStrings(Reader, SktmChunk)
		: TArray<FString>();
}
