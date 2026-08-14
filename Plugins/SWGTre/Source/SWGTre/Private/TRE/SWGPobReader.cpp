

#include "TRE/SWGPobReader.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGIFFChunkReader.h"
#include "TRE/SWGIffTags.h"
#include "Common/SWGWorldScale.h"

namespace
{
	bool ReadPobCollisionMesh(const FSWGIffReader& Reader, const FSWGIffChunk& CellVersionForm, TArray<FVector>& OutVertices, TArray<int32>& OutIndices)
	{
		FSWGIffChunk CmshForm;
		if (!Reader.FindChildForm(CellVersionForm, SWG_IFF_TAG('C', 'M', 'S', 'H'), CmshForm))
		{
			return true;
		}

		FSWGIffChunk Cmsh0000;
		FSWGIffChunk IdtlForm;
		FSWGIffChunk Idtl0000;
		if (!Reader.FindChildForm(CmshForm, SWG_IFF_TAG('0', '0', '0', '0'), Cmsh0000)
			|| !Reader.FindChildForm(Cmsh0000, SWG_IFF_TAG('I', 'D', 'T', 'L'), IdtlForm)
			|| !Reader.FindChildForm(IdtlForm, SWG_IFF_TAG('0', '0', '0', '0'), Idtl0000))
		{
			return false;
		}

		FSWGIffChunk VertChunk;
		if (Reader.FindChildChunk(Idtl0000, SWG_IFF_TAG('V', 'E', 'R', 'T'), VertChunk))
		{
			FSWGIFFChunkReader VertReader(VertChunk, Reader);
			const int32 VertexCount = Reader.GetChunkSize(VertChunk) / 12; // float32[3] per vertex
			OutVertices.Reserve(VertexCount);
			for (int32 i = 0; i < VertexCount; ++i)
			{
				OutVertices.Add(VertReader.ReadVectorLE<FVector, float>(SWGWorldScale));
			}
		}

		FSWGIffChunk IndxChunk;
		if (Reader.FindChildChunk(Idtl0000, SWG_IFF_TAG('I', 'N', 'D', 'X'), IndxChunk))
		{
			FSWGIFFChunkReader IndxReader(IndxChunk, Reader);
			const int32 IndexCount = Reader.GetChunkSize(IndxChunk) / 4; // int32 per index
			OutIndices.Reserve(IndexCount);
			for (int32 i = 0; i < IndexCount; ++i)
			{
				int32 Index = 0;
				if (!IndxReader.ReadValueLE(Index))
				{
					return false;
				}
				OutIndices.Add(Index);
			}
		}

		return true;
	}
}

bool FSWGPobReader::ReadPob(const FSWGIffReader& Reader, FSWGPobData& OutPob)
{
	OutPob.Cells.Reset();

	FSWGIffChunk PrtoForm;
	if (!Reader.FindForm(SWG_IFF_TAG('P', 'R', 'T', 'O'), PrtoForm))
	{
		return false;
	}

	TArray<FSWGIffChunk> PrtoChildren = Reader.ReadChildren(PrtoForm);
	if (PrtoChildren.Num() == 0 || !PrtoChildren[0].IsForm())
	{
		return false;
	}
	const FSWGIffChunk& VersionForm = PrtoChildren[0]; // "0003" or "0004"

	TArray<TArray<FVector>> GlobalPortalQuads;
	{
		FSWGIffChunk PrtsForm;
		if (!Reader.FindChildForm(VersionForm, SWG_IFF_TAG('P', 'R', 'T', 'S'), PrtsForm))
		{
			return false;
		}

		for (const FSWGIffChunk& PortalChunk : Reader.FindAllChildChunks(PrtsForm, SWG_IFF_TAG('P', 'R', 'T', 'L')))
		{
			FSWGIFFChunkReader PortalReader(PortalChunk, Reader);
			int32 CornerCount = 0;
			if (!PortalReader.ReadValueLE(CornerCount))
			{
				return false;
			}

			TArray<FVector>& Corners = GlobalPortalQuads.AddDefaulted_GetRef();
			Corners.Reserve(CornerCount);
			for (int32 i = 0; i < CornerCount; ++i)
			{
				Corners.Add(PortalReader.ReadVectorLE<FVector, float>(SWGWorldScale));
			}
		}
	}

	// --- Cells ---
	FSWGIffChunk CelsForm;
	if (!Reader.FindChildForm(VersionForm, SWG_IFF_TAG('C', 'E', 'L', 'S'), CelsForm))
	{
		return false;
	}

	TArray<FSWGIffChunk> CellForms = Reader.FindChildForms(CelsForm);
	OutPob.Cells.Reserve(CellForms.Num());

	for (int32 CellIndex = 0; CellIndex < CellForms.Num(); ++CellIndex)
	{
		const FSWGIffChunk& CellForm = CellForms[CellIndex];
		if (CellForm.FormType != SWG_IFF_TAG('C', 'E', 'L', 'L'))
		{
			continue;
		}

		TArray<FSWGIffChunk> CellVersionForms = Reader.ReadChildren(CellForm);
		if (CellVersionForms.Num() == 0 || !CellVersionForms[0].IsForm())
		{
			return false;
		}
		const FSWGIffChunk& CellVersionForm = CellVersionForms[0]; // "0004" or "0005"

		FSWGIffChunk DataChunk;
		if (!Reader.FindChildChunk(CellVersionForm, SWGIffTags::Data, DataChunk))
		{
			return false;
		}

		FSWGPobCell& Cell = OutPob.Cells.AddDefaulted_GetRef();
		Cell.CellIndex = CellIndex; // not itself stored in the file — this is the cell's position in CELS

		{
			FSWGIFFChunkReader DataReader(DataChunk, Reader);
			int32 NumberOfPortals = 0;
			uint8 CellFlags = 0;
			uint8 HasCollisionFloor = 0;
			if (!DataReader.ReadValueLE(NumberOfPortals)
				|| !DataReader.ReadValueLE(CellFlags)
				|| !DataReader.ReadTerminiatedString(Cell.CellName)
				|| !DataReader.ReadTerminiatedString(Cell.MeshPath)
				|| !DataReader.ReadValueLE(HasCollisionFloor)
				|| !DataReader.ReadTerminiatedString(Cell.CollisionFloorPath))
			{
				return false;
			}
			Cell.PortalCount = NumberOfPortals;
			Cell.CanSeeParent = CellFlags != 0; // cellFlags' meaning is otherwise unconfirmed (Sheet 00 §00.2)
		}

		// FORM CMSH (embedded collision mesh) or FORM NULL (Sheet 00 §00.5)
		if (!ReadPobCollisionMesh(Reader, CellVersionForm, Cell.CollisionVertices, Cell.CollisionIndices))
		{
			return false;
		}

		Cell.Portals.Reserve(Cell.PortalCount);
		for (const FSWGIffChunk& PrtlForm : Reader.FindChildForms(CellVersionForm))
		{
			if (PrtlForm.FormType != SWG_IFF_TAG('P', 'R', 'T', 'L'))
			{
				continue; // CMSH/NULL are also FORM children of this cell — not a portal
			}

			FSWGIffChunk PortalDataChunk;
			if (!Reader.FindChildChunk(PrtlForm, SWG_IFF_TAG('0', '0', '0', '4'), PortalDataChunk))
			{
				return false;
			}

			FSWGIFFChunkReader PortalReader(PortalDataChunk, Reader);
			FSWGPobPortalRef& Portal = Cell.Portals.AddDefaulted_GetRef();

			uint8 IsPassable = 0;
			uint8 IsClockwise = 0;
			uint8 HasHardpoint = 0;
			if (!PortalReader.ReadValueLE(IsPassable)
				|| !PortalReader.ReadValueLE(Portal.PortalNumber)
				|| !PortalReader.ReadValueLE(IsClockwise)
				|| !PortalReader.ReadValueLE(Portal.ConnectingCellIndex)
				|| !PortalReader.ReadTerminiatedString(Portal.DoorStyle)
				|| !PortalReader.ReadValueLE(HasHardpoint)
				|| !PortalReader.ReadTransform<FTransform, float>(Portal.DoorHardpoint, SWGWorldScale))
			{
				return false;
			}
			Portal.bIsPassable = IsPassable != 0;
			Portal.bIsGeometryWindingClockwise = IsClockwise != 0;
			Portal.bHasDoorHardpoint = HasHardpoint != 0;

			if (GlobalPortalQuads.IsValidIndex(Portal.PortalNumber))
			{
				Portal.OpeningVertices = GlobalPortalQuads[Portal.PortalNumber];
			}
		}

		// Per-cell lights: CHUNK LGHT (Sheet 00 §00.6) — int32 count
		// followed by that many packed 93-byte records, no padding.
		FSWGIffChunk LghtChunk;
		if (Reader.FindChildChunk(CellVersionForm, SWG_IFF_TAG('L', 'G', 'H', 'T'), LghtChunk))
		{
			FSWGIFFChunkReader LightReader(LghtChunk, Reader);
			int32 LightCount = 0;
			if (!LightReader.ReadValueLE(LightCount))
			{
				return false;
			}

			Cell.Lights.Reserve(LightCount);
			for (int32 i = 0; i < LightCount; ++i)
			{
				FSWGPobLight& Light = Cell.Lights.AddDefaulted_GetRef();

				uint8 LightType = 0;
				float Diffuse[4] = {};  // Alpha, Red, Green, Blue
				float Specular[4] = {}; // Alpha, Red, Green, Blue
				if (!LightReader.ReadValueLE(LightType)
					|| !LightReader.ReadValueLE(Diffuse[0]) || !LightReader.ReadValueLE(Diffuse[1])
					|| !LightReader.ReadValueLE(Diffuse[2]) || !LightReader.ReadValueLE(Diffuse[3])
					|| !LightReader.ReadValueLE(Specular[0]) || !LightReader.ReadValueLE(Specular[1])
					|| !LightReader.ReadValueLE(Specular[2]) || !LightReader.ReadValueLE(Specular[3])
					|| !LightReader.ReadTransform<FTransform, float>(Light.Transform, SWGWorldScale)
					|| !LightReader.ReadValueLE(Light.ConstantAttenuation)
					|| !LightReader.ReadValueLE(Light.LinearAttenuation)
					|| !LightReader.ReadValueLE(Light.QuadraticAttenuation))
				{
					return false;
				}

				Light.Type = static_cast<ESWGPobLightType>(LightType);
				Light.DiffuseColor = FLinearColor(Diffuse[1], Diffuse[2], Diffuse[3], Diffuse[0]);
				Light.SpecularColor = FLinearColor(Specular[1], Specular[2], Specular[3], Specular[0]);
			}
		}
	}

	return true;
}
