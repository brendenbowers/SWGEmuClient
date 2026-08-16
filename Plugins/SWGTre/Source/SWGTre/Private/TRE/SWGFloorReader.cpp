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
