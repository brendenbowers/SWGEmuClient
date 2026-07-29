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
	// Most .mgn PSDT PRIM forms use "OITL" ([uint16 flag] + 3×uint32 corner
	// indices per triangle), but some real submeshes (e.g. the sullustan_m
	// base mesh's head submesh, and others sharing this simpler shape) use
	// the plain "ITL " tag instead — the same one FORM MESH's static
	// submeshes use elsewhere, just under PRIM instead of directly under the
	// wrapper form. Both are handled below; a PRIM with neither is a genuine
	// parse failure.
	bool bHasOitl = Reader.FindChildChunk(PrimForm, SWG_IFF_TAG('O','I','T','L'), OitlChunk);
	FSWGIffChunk ItlChunk;
	bool bHasItl = !bHasOitl && Reader.FindChildChunk(PrimForm, SWG_IFF_TAG('I','T','L',' '), ItlChunk);
	if (!bHasOitl && !bHasItl)
	{
		FString ChildTags;
		for (const FSWGIffChunk& PrimChild : Reader.ReadChildren(PrimForm))
		{
			ChildTags += (PrimChild.IsForm() ? FString::Printf(TEXT("FORM %s "), *PrimChild.FormType.ToString()) : FString::Printf(TEXT("%s "), *PrimChild.Tag.ToString()));
		}
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s PRIM has neither OITL nor ITL (children: %s)"), *PsdtForm.FormType.ToString(), *ChildTags);
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
	int32 OutOfRangeCornerCount = 0;
	for (uint32 c = 0; c < CornerCount; ++c)
	{
		uint32 PosIndex = PidxReader.ReadValueLE<uint32>();
		uint32 NormIndex = NidxReader.ReadValueLE<uint32>();
		// A handful of real .mgn files (e.g. hum_f_arms_l0.mgn, hum_f_body_l0.mgn)
		// carry a stray corner whose index is exactly Positions/Normals.Num() —
		// one past the last valid entry, not a randomly corrupt value. Previously
		// this aborted the whole submesh (return false), which is why entire
		// body parts went missing from merged skeletal meshes even though only
		// one corner out of hundreds was bad. Clamp instead of aborting: corner
		// indices must stay dense (OITL triangles reference them by position in
		// OutSubmesh.Vertices), so the corner can't just be skipped either.
		if ((int32)PosIndex >= Positions.Num() || (int32)NormIndex >= Normals.Num())
		{
			++OutOfRangeCornerCount;
			PosIndex = FMath::Clamp(PosIndex, 0u, (uint32)FMath::Max(0, Positions.Num() - 1));
			NormIndex = FMath::Clamp(NormIndex, 0u, (uint32)FMath::Max(0, Normals.Num() - 1));
		}

		FSWGMeshVertex Vertex;
		Vertex.Position = Positions[PosIndex];
		Vertex.Normal = Normals[NormIndex];
		Vertex.SourcePositionIndex = (int32)PosIndex;
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
	if (OutOfRangeCornerCount > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: PSDT %s had %d/%d corner(s) with out-of-range Pos/NormIndex — clamped instead of dropping the submesh"),
			*PsdtForm.FormType.ToString(), OutOfRangeCornerCount, CornerCount);
	}

	if (bHasOitl)
	{
		// OITL record = [uint16 flag][uint32 corner0][uint32 corner1][uint32 corner2]
		// = 14 bytes — confirmed against a real chunk (DataSize=2160: (2160-4)/14
		// = 154 exactly, whereas /12 isn't an integer).
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
	}
	else
	{
		// Plain ITL record = 3×uint32 corner indices, no leading flag. Some
		// real files carry a leading uint32 TriCount prefix (like OITL does)
		// and some don't (DataSize alone is an exact multiple of 12) — detect
		// which by whether DataSize divides evenly by 12.
		FSWGIFFChunkReader ItlReader(ItlChunk, Reader);
		uint32 TriCount;
		if (ItlChunk.DataSize % 12 == 0)
		{
			TriCount = (uint32)ItlChunk.DataSize / 12;
		}
		else
		{
			TriCount = ItlReader.ReadValueLE<uint32>();
		}
		OutSubmesh.Triangles.Reserve((int32)TriCount * 3);
		for (uint32 t = 0; t < TriCount; ++t)
		{
			OutSubmesh.Triangles.Add((int32)ItlReader.ReadValueLE<uint32>());
			OutSubmesh.Triangles.Add((int32)ItlReader.ReadValueLE<uint32>());
			OutSubmesh.Triangles.Add((int32)ItlReader.ReadValueLE<uint32>());
		}
	}

	return true;
}

bool FSWGMeshReader::ReadSkeletalMeshBindPose(const FSWGIffReader& Reader, FSWGMeshData& OutMesh)
{
	if (!Reader.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: ReadSkeletalMeshBindPose - reader is invalid"));
		return false;
	}

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0 || !TopLevel[0].IsForm() || TopLevel[0].FormType != SWG_IFF_TAG('S','K','M','G'))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: ReadSkeletalMeshBindPose - top level is not FORM SKMG (TopLevel.Num()=%d, first=%s%s)"),
			TopLevel.Num(),
			TopLevel.Num() > 0 && TopLevel[0].IsForm() ? *TopLevel[0].FormType.ToString() : (TopLevel.Num() > 0 ? *TopLevel[0].Tag.ToString() : TEXT("<none>")),
			TopLevel.Num() > 0 && TopLevel[0].IsForm() ? TEXT(" (form)") : TEXT(" (chunk)"));
		return false;
	}

	const FSWGIffChunk& SkmgForm = TopLevel[0];

	// Same version-tag drift as FORM MESH's inner form (see ReadStaticMesh) —
	// hardcoded "0004" doesn't match every real .mgn; take whichever single
	// version-tagged form is actually present.
	const TArray<FSWGIffChunk> SkmgVersionForms = Reader.FindChildForms(SkmgForm);
	if (SkmgVersionForms.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: ReadSkeletalMeshBindPose - FORM SKMG has no version child forms"));
		return false;
	}
	const FSWGIffChunk& Form0004 = SkmgVersionForms[0];

	FSWGIffChunk PosnChunk, NormChunk;
	if (!Reader.FindChildChunk(Form0004, SWG_IFF_TAG('P','O','S','N'), PosnChunk))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: ReadSkeletalMeshBindPose - version form %s missing POSN chunk"), *Form0004.FormType.ToString());
		return false;
	}
	if (!Reader.FindChildChunk(Form0004, SWG_IFF_TAG('N','O','R','M'), NormChunk))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: ReadSkeletalMeshBindPose - version form %s missing NORM chunk"), *Form0004.FormType.ToString());
		return false;
	}

	FSWGIFFChunkReader PosnReader(PosnChunk, Reader);
	FSWGIFFChunkReader NormReader(NormChunk, Reader);
	const int32 VertexCount = PosnChunk.DataSize / 12;
	// NORM isn't guaranteed to carry one entry per POSN vertex — some real
	// .mgn files (hum_f_arms_l0.mgn, hum_f_body_l0.mgn, and similar arm/body
	// parts across many species) have more normal entries than position
	// entries. Reusing VertexCount to bound both reads truncated the Normals
	// array to the (smaller) position count, so legitimate NIDX values from
	// PSDT corners further down read as "out of range" and the whole submesh
	// was dropped — this is what caused entire body parts to silently vanish
	// from merged skeletal meshes.
	const int32 NormalCount = NormChunk.DataSize / 12;

	// .mgn position data is in meters, same as .msh, and needs the same
	// scale-up to match the rest of the codebase's world-unit convention;
	// normals stay unscaled (unit-length).
	TArray<FVector> Positions, Normals;
	Positions.Reserve(VertexCount);
	Normals.Reserve(NormalCount);
	for (int32 i = 0; i < VertexCount; ++i)
	{
		Positions.Add(PosnReader.ReadVectorLE<FVector, float>(SWGWorldScale));
	}
	for (int32 i = 0; i < NormalCount; ++i)
	{
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

	ReadHardpoints(Reader, Form0004, OutMesh);
	ReadBlendTargets(Reader, Form0004, OutMesh);

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

void FSWGMeshReader::ReadHardpoints(const FSWGIffReader& Reader, const FSWGIffChunk& Form0004, FSWGMeshData& OutMesh)
{
	FSWGIffChunk HptsForm;
	if (!Reader.FindChildForm(Form0004, SWG_IFF_TAG('H','P','T','S'), HptsForm))
	{
		return;
	}

	for (const FSWGIffChunk& Child : Reader.ReadChildren(HptsForm))
	{
		if (Child.Tag != SWG_IFF_TAG('S','T','A','T') && Child.Tag != SWG_IFF_TAG('D','Y','N',' '))
		{
			continue;
		}

		FSWGIFFChunkReader HardpointReader(Child, Reader);
		const int32 Count = HardpointReader.ReadValueLE<int16>();
		if (Count < 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: invalid %s hardpoint count %d"), *Child.Tag.ToString(), Count);
			continue;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FSWGMeshHardpoint Hardpoint;
			if (!HardpointReader.ReadTerminiatedString(Hardpoint.Name)
				|| !HardpointReader.ReadTerminiatedString(Hardpoint.ParentName)
				|| !HardpointReader.ReadQuatLE<FQuat, float>(Hardpoint.Rotation)
				|| !HardpointReader.ReadVectorLE<FVector, float>(Hardpoint.Translation, SWGWorldScale))
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: malformed %s hardpoint %d of %d"),
					*Child.Tag.ToString(), Index + 1, Count);
				break;
			}

			if (!Hardpoint.Name.IsEmpty() && !Hardpoint.ParentName.IsEmpty())
			{
				OutMesh.Hardpoints.Add(MoveTemp(Hardpoint));
			}
		}
	}
}

void FSWGMeshReader::ReadBlendTargets(const FSWGIffReader& Reader, const FSWGIffChunk& Form0004, FSWGMeshData& OutMesh)
{
	FSWGIffChunk BltsForm;
	if (!Reader.FindChildForm(Form0004, SWG_IFF_TAG('B','L','T','S'), BltsForm))
	{
		return; // Most parts (arms, hands, ...) carry no blend shapes at all — not an error.
	}

	for (const FSWGIffChunk& BltForm : Reader.FindChildForms(BltsForm))
	{
		if (BltForm.FormType != SWG_IFF_TAG('B','L','T',' ')) continue;

		FSWGIffChunk InfoChunk, PosnChunk;
		if (!Reader.FindChildChunk(BltForm, SWGIffTags::Info, InfoChunk))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: FORM BLT missing INFO — skipping this blend target"));
			continue;
		}

		FSWGIFFChunkReader InfoReader(InfoChunk, Reader);
		const int32 PosnDeltaCount = InfoReader.ReadValueLE<int32>();
		InfoReader.Skip<int32>(); // NormDeltaCount — normal deltas aren't consumed (see header comment)
		FString Name;
		InfoReader.ReadTerminiatedString(Name);

		if (Name.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: FORM BLT has no name in its INFO chunk — skipping"));
			continue;
		}
		if (!Reader.FindChildChunk(BltForm, SWG_IFF_TAG('P','O','S','N'), PosnChunk))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: blend target '%s' missing POSN — skipping"), *Name);
			continue;
		}

		FSWGMeshMorphTarget MorphTarget;
		MorphTarget.Name = Name;

		FSWGIFFChunkReader PosnReader(PosnChunk, Reader);
		// PosnDeltaCount (from INFO) is authoritative; PosnChunk.DataSize/16
		// should agree but isn't assumed to (defends against a corrupt/
		// mismatched INFO count reading past the chunk).
		for (int32 i = 0; i < PosnDeltaCount && PosnReader.CanRead<uint32>() && PosnReader.CanRead<float>(); ++i)
		{
			const uint32 Index = PosnReader.ReadValueLE<uint32>();
			const FVector Delta = PosnReader.ReadVectorLE<FVector, float>(SWGWorldScale);
			MorphTarget.PositionDeltas.Add((int32)Index, Delta);
		}

		UE_LOG(LogTemp, Warning, TEXT("FSWGMeshReader: blend target '%s' — %d delta(s) (INFO said %d)"), *MorphTarget.Name, MorphTarget.PositionDeltas.Num(), PosnDeltaCount);
		OutMesh.MorphTargets.Add(MoveTemp(MorphTarget));
	}
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
