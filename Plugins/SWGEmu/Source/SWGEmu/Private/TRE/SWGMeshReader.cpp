#include "TRE/SWGMeshReader.h"

namespace
{
	float ReadFloatLE(const uint8* Data, int32 Offset)
	{
		float Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(float));
		return Value;
	}

	uint32 ReadUInt32LE(const uint8* Data, int32 Offset)
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
		return FVector(ReadFloatLE(Data, Offset), ReadFloatLE(Data, Offset + 4), ReadFloatLE(Data, Offset + 8));
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
	const FVector A = ReadVector3LE(Data, 0);
	const FVector B = ReadVector3LE(Data, 12);

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

	FSWGIffChunk Wrapper0001, Vtxa, F0003, InfoChunk, DataChunk, IndxChunk;
	if (!FindChildForm(Reader, SubmeshForm, TEXT("0001"), Wrapper0001)) return false;
	if (!FindChildForm(Reader, Wrapper0001, TEXT("VTXA"), Vtxa)) return false;
	if (!FindChildForm(Reader, Vtxa, TEXT("0003"), F0003)) return false;
	if (!FindChildChunk(Reader, F0003, TEXT("INFO"), InfoChunk)) return false;
	if (!FindChildChunk(Reader, F0003, TEXT("DATA"), DataChunk)) return false;
	if (!FindChildChunk(Reader, SubmeshForm, TEXT("INDX"), IndxChunk)) return false;

	const uint8* InfoData = Reader.GetChunkData(InfoChunk);
	const uint32 Flags = ReadUInt32LE(InfoData, 0);
	const uint32 VertexCount = ReadUInt32LE(InfoData, 4);
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
		Vertex.Position = ReadVector3LE(VertexData, Base + 0);
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
			const float U = ReadFloatLE(VertexData, UvStart + ch * 8);
			const float V = ReadFloatLE(VertexData, UvStart + ch * 8 + 4);
			Vertex.UVs.Add(FVector2D(U, V));
		}

		OutSubmesh.Vertices.Add(MoveTemp(Vertex));
	}

	const uint8* IndxData = Reader.GetChunkData(IndxChunk);
	const uint32 TriIndexCount = ReadUInt32LE(IndxData, 0);
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

	FSWGIffChunk Form0004;
	if (!FindChildForm(Reader, MeshForm, TEXT("0004"), Form0004)) return false;

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
	}

	return OutMesh.Submeshes.Num() > 0;
}

bool FSWGMeshReader::ReadMgnSubmesh(const FSWGIffReader& Reader, const FSWGIffChunk& PsdtForm, const TArray<FVector>& Positions, const TArray<FVector>& Normals, FSWGMeshSubmesh& OutSubmesh)
{
	FSWGIffChunk NameChunk;
	if (FindChildChunk(Reader, PsdtForm, TEXT("NAME"), NameChunk))
	{
		OutSubmesh.ShaderName = ReadNullTerminatedString(Reader, NameChunk);
	}

	FSWGIffChunk PidxChunk, NidxChunk, TcsfForm, TcsdChunk, PrimForm, OitlChunk;
	if (!FindChildChunk(Reader, PsdtForm, TEXT("PIDX"), PidxChunk)) return false;
	if (!FindChildChunk(Reader, PsdtForm, TEXT("NIDX"), NidxChunk)) return false;
	if (!FindChildForm(Reader, PsdtForm, TEXT("TCSF"), TcsfForm)) return false;
	if (!FindChildChunk(Reader, TcsfForm, TEXT("TCSD"), TcsdChunk)) return false;
	if (!FindChildForm(Reader, PsdtForm, TEXT("PRIM"), PrimForm)) return false;
	if (!FindChildChunk(Reader, PrimForm, TEXT("OITL"), OitlChunk)) return false;

	// PIDX carries its own leading count prefix; NIDX/TCSD do not (their counts
	// are implied to match PIDX's) — confirmed against real samples, see
	// world-object-plan.html "Parsing gotcha hit while decoding this".
	const uint8* PidxData = Reader.GetChunkData(PidxChunk);
	const uint32 CornerCount = ReadUInt32LE(PidxData, 0);
	if (CornerCount == 0) return false;

	const uint8* NidxData = Reader.GetChunkData(NidxChunk);
	const uint8* TcsdData = Reader.GetChunkData(TcsdChunk);

	OutSubmesh.Vertices.Reserve((int32)CornerCount);
	for (uint32 c = 0; c < CornerCount; ++c)
	{
		const uint32 PosIndex = ReadUInt32LE(PidxData, 4 + (int32)c * 4);
		const uint32 NormIndex = ReadUInt32LE(NidxData, (int32)c * 4);
		if ((int32)PosIndex >= Positions.Num() || (int32)NormIndex >= Normals.Num())
			return false;

		FSWGMeshVertex Vertex;
		Vertex.Position = Positions[PosIndex];
		Vertex.Normal = Normals[NormIndex];
		Vertex.UVs.Add(FVector2D(ReadFloatLE(TcsdData, (int32)c * 8), ReadFloatLE(TcsdData, (int32)c * 8 + 4)));
		OutSubmesh.Vertices.Add(MoveTemp(Vertex));
	}

	// OITL record = [uint16 flag][uint32 corner0][uint32 corner1][uint32 corner2] = 14 bytes,
	// corner indices reference the CornerCount-sized array we just built directly.
	const uint8* OitlData = Reader.GetChunkData(OitlChunk);
	const uint32 TriCount = ReadUInt32LE(OitlData, 0);
	constexpr int32 RecordBytes = 14;
	OutSubmesh.Triangles.Reserve((int32)TriCount * 3);
	for (uint32 t = 0; t < TriCount; ++t)
	{
		const int32 RecordOffset = 4 + (int32)t * RecordBytes;
		OutSubmesh.Triangles.Add((int32)ReadUInt32LE(OitlData, RecordOffset + 2));
		OutSubmesh.Triangles.Add((int32)ReadUInt32LE(OitlData, RecordOffset + 6));
		OutSubmesh.Triangles.Add((int32)ReadUInt32LE(OitlData, RecordOffset + 10));
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

	FSWGIffChunk Form0004;
	if (!FindChildForm(Reader, SkmgForm, TEXT("0004"), Form0004)) return false;

	FSWGIffChunk PosnChunk, NormChunk;
	if (!FindChildChunk(Reader, Form0004, TEXT("POSN"), PosnChunk)) return false;
	if (!FindChildChunk(Reader, Form0004, TEXT("NORM"), NormChunk)) return false;

	const uint8* PosnData = Reader.GetChunkData(PosnChunk);
	const uint8* NormData = Reader.GetChunkData(NormChunk);
	const int32 VertexCount = PosnChunk.DataSize / 12;

	TArray<FVector> Positions, Normals;
	Positions.Reserve(VertexCount);
	Normals.Reserve(VertexCount);
	for (int32 i = 0; i < VertexCount; ++i)
	{
		Positions.Add(ReadVector3LE(PosnData, i * 12));
		Normals.Add(ReadVector3LE(NormData, i * 12));
	}

	for (const FSWGIffChunk& Child : FindChildForms(Reader, Form0004))
	{
		if (Child.FormType != TEXT("PSDT")) continue;

		FSWGMeshSubmesh Submesh;
		if (ReadMgnSubmesh(Reader, Child, Positions, Normals, Submesh))
		{
			OutMesh.Submeshes.Add(MoveTemp(Submesh));
		}
	}

	return OutMesh.Submeshes.Num() > 0;
}
