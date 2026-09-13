#include "TRE/SWGAppearanceCollisionReader.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGIFFChunkReader.h"
#include "TRE/SWGIffTags.h"
#include "Common/SWGWorldScale.h"

namespace
{
	const FSWGIffTag TagAppr = SWG_IFF_TAG('A','P','P','R');
	const FSWGIffTag TagExbx = SWG_IFF_TAG('E','X','B','X');
	const FSWGIffTag TagExsp = SWG_IFF_TAG('E','X','S','P');
	const FSWGIffTag TagXcyl = SWG_IFF_TAG('X','C','Y','L');
	const FSWGIffTag TagCmsh = SWG_IFF_TAG('C','M','S','H');
	const FSWGIffTag TagCmpt = SWG_IFF_TAG('C','M','P','T');
	const FSWGIffTag TagDtal = SWG_IFF_TAG('D','T','A','L');
	const FSWGIffTag TagNull = SWG_IFF_TAG('N','U','L','L');
	const FSWGIffTag TagCpst = SWG_IFF_TAG('C','P','S','T');
	const FSWGIffTag TagIdtl = SWG_IFF_TAG('I','D','T','L');
	const FSWGIffTag TagFlor = SWG_IFF_TAG('F','L','O','R');
	const FSWGIffTag TagSphr = SWG_IFF_TAG('S','P','H','R');
	const FSWGIffTag TagBox  = SWG_IFF_TAG('B','O','X',' ');
	const FSWGIffTag TagCyln = SWG_IFF_TAG('C','Y','L','N');
	const FSWGIffTag TagVert = SWG_IFF_TAG('V','E','R','T');
	const FSWGIffTag TagIndx = SWG_IFF_TAG('I','N','D','X');

	/** The single version form under an extent form (0000/0001/...), whichever it is. */
	bool FindVersionForm(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, FSWGIffChunk& OutVersion)
	{
		const TArray<FSWGIffChunk> Forms = Reader.FindChildForms(Parent);
		if (Forms.IsEmpty())
		{
			return false;
		}
		OutVersion = Forms[0];
		return true;
	}
}

bool FSWGAppearanceCollisionReader::IsExtentForm(const FSWGIffChunk& Form)
{
	if (!Form.IsForm())
	{
		return false;
	}
	return Form.FormType == TagExbx || Form.FormType == TagExsp || Form.FormType == TagXcyl
		|| Form.FormType == TagCmsh || Form.FormType == TagCmpt || Form.FormType == TagDtal || Form.FormType == TagNull;
}

bool FSWGAppearanceCollisionReader::Read(const FSWGIffReader& Reader, FSWGAppearanceCollision& OutCollision)
{
	FSWGIffChunk ApprForm, VersionForm;
	if (!Reader.IsValid() || !Reader.FindForm(TagAppr, ApprForm) || !FindVersionForm(Reader, ApprForm, VersionForm))
	{
		return false;
	}

	// In order: the bounding extent, then the collision extent (FORM NULL
	// when none was authored), then HPTS, FLOR and whatever else the version adds.
	int32 ExtentsSeen = 0;
	for (const FSWGIffChunk& Child : Reader.ReadChildren(VersionForm))
	{
		if (IsExtentForm(Child))
		{
			FSWGCollisionExtent Extent;
			if (!ReadExtent(Reader, Child, Extent))
			{
				return false;
			}
			if (ExtentsSeen == 0)
			{
				OutCollision.BoundingExtent = MoveTemp(Extent);
			}
			else if (ExtentsSeen == 1 && Extent.Type != ESWGCollisionExtentType::Null)
			{
				OutCollision.CollisionExtent = MoveTemp(Extent);
			}
			++ExtentsSeen;
			continue;
		}

		if (Child.IsForm() && Child.FormType == TagFlor)
		{
			FSWGIffChunk DataChunk;
			if (Reader.FindChildChunk(Child, SWGIffTags::Data, DataChunk))
			{
				FSWGIFFChunkReader FloorReader(DataChunk, Reader);
				uint8 bHasFloor = 0;
				if (FloorReader.ReadValueLE(bHasFloor) && bHasFloor != 0)
				{
					FloorReader.ReadTerminiatedString(OutCollision.FloorPath);
				}
			}
		}
	}

	return OutCollision.HasAnything();
}

bool FSWGAppearanceCollisionReader::ReadChildrenExtents(const FSWGIffReader& Reader, const FSWGIffChunk& Form, TArray<FSWGCollisionExtent>& OutChildren)
{
	// CMPT/DTAL > 000x > CPST > 000x > extents
	FSWGIffChunk VersionForm, CpstForm, CpstVersion;
	if (!FindVersionForm(Reader, Form, VersionForm)
		|| !Reader.FindChildForm(VersionForm, TagCpst, CpstForm)
		|| !FindVersionForm(Reader, CpstForm, CpstVersion))
	{
		return false;
	}

	for (const FSWGIffChunk& Child : Reader.ReadChildren(CpstVersion))
	{
		if (!IsExtentForm(Child))
		{
			continue;
		}
		FSWGCollisionExtent Extent;
		if (ReadExtent(Reader, Child, Extent) && Extent.Type != ESWGCollisionExtentType::Null)
		{
			OutChildren.Add(MoveTemp(Extent));
		}
	}
	return true;
}

bool FSWGAppearanceCollisionReader::ReadExtent(const FSWGIffReader& Reader, const FSWGIffChunk& Form, FSWGCollisionExtent& OutExtent)
{
	if (Form.FormType == TagNull)
	{
		OutExtent.Type = ESWGCollisionExtentType::Null;
		return true;
	}

	if (Form.FormType == TagCmpt || Form.FormType == TagDtal)
	{
		OutExtent.Type = Form.FormType == TagCmpt ? ESWGCollisionExtentType::Composite : ESWGCollisionExtentType::Detail;
		return ReadChildrenExtents(Reader, Form, OutExtent.Children);
	}

	FSWGIffChunk VersionForm;
	if (!FindVersionForm(Reader, Form, VersionForm))
	{
		return false;
	}

	if (Form.FormType == TagExbx)
	{
		FSWGIffChunk BoxChunk;
		if (!Reader.FindChildChunk(VersionForm, TagBox, BoxChunk))
		{
			return false;
		}
		FSWGIFFChunkReader BoxReader(BoxChunk, Reader);
		// Written max then min; the Y/Z swap can reorder components, so rebuild the box from both corners.
		const FVector CornerA = BoxReader.ReadVectorLE<FVector, float>(SWGWorldScale);
		const FVector CornerB = BoxReader.ReadVectorLE<FVector, float>(SWGWorldScale);
		OutExtent.Type = ESWGCollisionExtentType::Box;
		OutExtent.Box = FBox(ForceInit);
		OutExtent.Box += CornerA;
		OutExtent.Box += CornerB;
		return true;
	}

	if (Form.FormType == TagExsp)
	{
		FSWGIffChunk SphereChunk;
		if (!Reader.FindChildChunk(VersionForm, TagSphr, SphereChunk))
		{
			return false;
		}
		FSWGIFFChunkReader SphereReader(SphereChunk, Reader);
		OutExtent.Type = ESWGCollisionExtentType::Sphere;
		OutExtent.Center = SphereReader.ReadVectorLE<FVector, float>(SWGWorldScale);
		float Radius = 0.0f;
		SphereReader.ReadValueLE(Radius);
		OutExtent.Radius = SWGToUnrealSpace(Radius);
		return true;
	}

	if (Form.FormType == TagXcyl)
	{
		FSWGIffChunk CylinderChunk;
		if (!Reader.FindChildChunk(VersionForm, TagCyln, CylinderChunk))
		{
			return false;
		}
		FSWGIFFChunkReader CylinderReader(CylinderChunk, Reader);
		OutExtent.Type = ESWGCollisionExtentType::Cylinder;
		OutExtent.Center = CylinderReader.ReadVectorLE<FVector, float>(SWGWorldScale);
		float Radius = 0.0f, Height = 0.0f;
		CylinderReader.ReadValueLE(Radius);
		CylinderReader.ReadValueLE(Height);
		OutExtent.Radius = SWGToUnrealSpace(Radius);
		OutExtent.Height = SWGToUnrealSpace(Height);
		return true;
	}

	if (Form.FormType == TagCmsh)
	{
		FSWGIffChunk IdtlForm, IdtlVersion, VertChunk, IndxChunk;
		if (!Reader.FindChildForm(VersionForm, TagIdtl, IdtlForm)
			|| !FindVersionForm(Reader, IdtlForm, IdtlVersion)
			|| !Reader.FindChildChunk(IdtlVersion, TagVert, VertChunk)
			|| !Reader.FindChildChunk(IdtlVersion, TagIndx, IndxChunk))
		{
			return false;
		}

		OutExtent.Type = ESWGCollisionExtentType::Mesh;

		FSWGIFFChunkReader VertReader(VertChunk, Reader);
		const int32 VertexCount = VertChunk.DataSize / 12;
		OutExtent.Vertices.Reserve(VertexCount);
		for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
		{
			OutExtent.Vertices.Add(VertReader.ReadVectorLE<FVector, float>(SWGWorldScale));
		}

		FSWGIFFChunkReader IndxReader(IndxChunk, Reader);
		const int32 IndexCount = IndxChunk.DataSize / 4;
		OutExtent.Indices.Reserve(IndexCount);
		for (int32 IndexIndex = 0; IndexIndex < IndexCount; ++IndexIndex)
		{
			int32 Index = 0;
			IndxReader.ReadValueLE(Index);
			OutExtent.Indices.Add(Index);
		}
		return OutExtent.Vertices.Num() >= 3 && OutExtent.Indices.Num() >= 3;
	}

	return false;
}
