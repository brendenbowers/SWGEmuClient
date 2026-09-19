#include "TRE/SWGFloorReader.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGIFFChunkReader.h"
#include "Common/SWGWorldScale.h"

bool FSWGFloorReader::ReadFloor(const FSWGIffReader& Reader, FSWGFloorData& OutFloor)
{
	OutFloor.Vertices.Reset();
	OutFloor.Triangles.Reset();

	FSWGIffChunk FlorForm;
	if (!Reader.FindForm(SWG_IFF_TAG('F', 'L', 'O', 'R'), FlorForm))
	{
		return false;
	}

	TArray<FSWGIffChunk> FlorChildren = Reader.ReadChildren(FlorForm);
	if (FlorChildren.Num() == 0 || !FlorChildren[0].IsForm() || FlorChildren[0].FormType != SWG_IFF_TAG('0', '0', '0', '6'))
	{
		// Only FORM 0006 is decoded — see this reader's own comment on the
		// undecoded FORM 0003 layout seen on non-player-house content.
		return false;
	}
	const FSWGIffChunk& InnerForm = FlorChildren[0];

	FSWGIffChunk VertChunk, TrisChunk;
	if (!Reader.FindChildChunk(InnerForm, SWG_IFF_TAG('V', 'E', 'R', 'T'), VertChunk)
		|| !Reader.FindChildChunk(InnerForm, SWG_IFF_TAG('T', 'R', 'I', 'S'), TrisChunk))
	{
		return false;
	}

	{
		FSWGIFFChunkReader VertReader(VertChunk, Reader);
		int32 VertexCount = 0;
		if (!VertReader.ReadValueLE(VertexCount))
		{
			return false;
		}
		OutFloor.Vertices.Reserve(VertexCount);
		for (int32 i = 0; i < VertexCount; ++i)
		{
			OutFloor.Vertices.Add(VertReader.ReadVectorLE<FVector, float>(SWGWorldScale));
		}
	}

	{
		FSWGIFFChunkReader TrisReader(TrisChunk, Reader);
		int32 TriCount = 0;
		if (!TrisReader.ReadValueLE(TriCount))
		{
			return false;
		}
		OutFloor.Triangles.Reserve(TriCount);
		for (int32 i = 0; i < TriCount; ++i)
		{
			FSWGFloorTriangle& Tri = OutFloor.Triangles.AddDefaulted_GetRef();
			uint8 FallThrough = 0;
			bool bReadOk = TrisReader.ReadValueLE(Tri.CornerIndex1)
				&& TrisReader.ReadValueLE(Tri.CornerIndex2)
				&& TrisReader.ReadValueLE(Tri.CornerIndex3)
				&& TrisReader.ReadValueLE(Tri.Index)
				&& TrisReader.ReadValueLE(Tri.NeighborIndex1)
				&& TrisReader.ReadValueLE(Tri.NeighborIndex2)
				&& TrisReader.ReadValueLE(Tri.NeighborIndex3);
			if (bReadOk)
			{
				// A direction, not a position — WorldScale=1 keeps it unit length.
				Tri.Normal = TrisReader.ReadVectorLE<FVector, float>(1.0f);
			}
			bReadOk = bReadOk
				&& TrisReader.ReadValueLE(Tri.EdgeType1)
				&& TrisReader.ReadValueLE(Tri.EdgeType2)
				&& TrisReader.ReadValueLE(Tri.EdgeType3)
				&& TrisReader.ReadValueLE(FallThrough)
				&& TrisReader.ReadValueLE(Tri.PartTag)
				&& TrisReader.ReadValueLE(Tri.PortalId1)
				&& TrisReader.ReadValueLE(Tri.PortalId2)
				&& TrisReader.ReadValueLE(Tri.PortalId3);
			if (!bReadOk)
			{
				return false;
			}
			Tri.bFallThrough = FallThrough != 0;
		}
	}

	return true;
}


void FSWGFloorReader::AppendFloorTriangles(const FSWGFloorData& Floor, TArray<int32>& OutIndices)
{
	OutIndices.Reserve(OutIndices.Num() + Floor.Triangles.Num() * 3);
	for (const FSWGFloorTriangle& Triangle : Floor.Triangles)
	{
		if (!Floor.Vertices.IsValidIndex(Triangle.CornerIndex1) || !Floor.Vertices.IsValidIndex(Triangle.CornerIndex2) || !Floor.Vertices.IsValidIndex(Triangle.CornerIndex3))
		{
			continue;
		}
		const FVector& CornerA = Floor.Vertices[Triangle.CornerIndex1];
		const FVector& CornerB = Floor.Vertices[Triangle.CornerIndex2];
		const FVector& CornerC = Floor.Vertices[Triangle.CornerIndex3];
		// Chaos treats (C-A)x(B-A) as the front — verified in-game: the other
		// way round, floors blocked from below and were air from above.
		const bool bFacesUp = FVector::CrossProduct(CornerB - CornerA, CornerC - CornerA).Z <= 0.0;
		OutIndices.Add(Triangle.CornerIndex1);
		OutIndices.Add(bFacesUp ? Triangle.CornerIndex2 : Triangle.CornerIndex3);
		OutIndices.Add(bFacesUp ? Triangle.CornerIndex3 : Triangle.CornerIndex2);
	}
}

int32 FSWGFloorReader::AppendBarrierMesh(const FSWGFloorData& Floor, float Height, TArray<FVector>& OutVertices, TArray<int32>& OutIndices)
{
	int32 Barriers = 0;
	const FVector Up(0.0f, 0.0f, Height);

	for (const FSWGFloorTriangle& Triangle : Floor.Triangles)
	{
		const int32 Corners[3] = { Triangle.CornerIndex1, Triangle.CornerIndex2, Triangle.CornerIndex3 };
		const uint8 EdgeTypes[3] = { Triangle.EdgeType1, Triangle.EdgeType2, Triangle.EdgeType3 };

		for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
		{
			if (EdgeTypes[EdgeIndex] == (uint8)ESWGFloorEdgeType::Crossable)
			{
				continue;
			}
			const int32 StartIndex = Corners[EdgeIndex];
			const int32 EndIndex = Corners[(EdgeIndex + 1) % 3];
			if (!Floor.Vertices.IsValidIndex(StartIndex) || !Floor.Vertices.IsValidIndex(EndIndex))
			{
				continue;
			}

			const FVector& Start = Floor.Vertices[StartIndex];
			const FVector& End = Floor.Vertices[EndIndex];
			const int32 Base = OutVertices.Num();
			OutVertices.Add(Start);
			OutVertices.Add(End);
			OutVertices.Add(End + Up);
			OutVertices.Add(Start + Up);
			OutIndices.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
			++Barriers;
		}
	}
	return Barriers;
}
