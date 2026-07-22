#include "TRE/SWGSkeletonReader.h"
#include "TRE/SWGIffTags.h"
#include "TRE/SWGIFFChunkReader.h"
#include "Common/SWGWorldScale.h"

bool FSWGSkeletonReader::ReadSkeleton(const FSWGIffReader& Reader, FSWGSkeletonData& OutSkeleton)
{
	if (!Reader.IsValid())
	{
		return false;
	}

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0 || !TopLevel[0].IsForm() || TopLevel[0].FormType != SWG_IFF_TAG('S','L','O','D'))
		return false;

	const FSWGIffChunk& SlodForm = TopLevel[0];

	// Same version-tag-drift pattern as FSWGMeshReader's FORM MESH/SKMG outer
	// wrapper — take whichever single version-tagged form is present rather
	// than hardcode "0000".
	const TArray<FSWGIffChunk> SlodVersionForms = Reader.FindChildForms(SlodForm);
	if (SlodVersionForms.Num() == 0) return false;
	const FSWGIffChunk& Form0000 = SlodVersionForms[0];

	// FORM SLOD > FORM 0000 wraps one FORM SKTM per skeleton LOD level, most
	// detailed (highest joint count) first — only that first one is used;
	// there's no current need for the coarser LODs the game uses for distant
	// creatures.
	FSWGIffChunk SktmForm;
	if (!Reader.FindChildForm(Form0000, SWG_IFF_TAG('S','K','T','M'), SktmForm)) return false;

	// FORM SKTM wraps a single version-tagged form too, same pattern again.
	const TArray<FSWGIffChunk> SktmVersionForms = Reader.FindChildForms(SktmForm);
	if (SktmVersionForms.Num() == 0) return false;
	const FSWGIffChunk& SktmInner = SktmVersionForms[0];

	FSWGIffChunk InfoChunk, NameChunk, PrntChunk, BptrChunk, BproChunk;
	if (!Reader.FindChildChunk(SktmInner, SWGIffTags::Info, InfoChunk)) return false;
	if (!Reader.FindChildChunk(SktmInner, SWGIffTags::Name, NameChunk)) return false;
	if (!Reader.FindChildChunk(SktmInner, SWG_IFF_TAG('P','R','N','T'), PrntChunk)) return false;
	if (!Reader.FindChildChunk(SktmInner, SWG_IFF_TAG('B','P','T','R'), BptrChunk)) return false;
	if (!Reader.FindChildChunk(SktmInner, SWG_IFF_TAG('B','P','R','O'), BproChunk)) return false;

	// RPRE/RPST (per-joint pre/post rotation quaternions) are optional-safe:
	// missing chunks leave every joint at identity, which degrades to the old
	// (pre-fix) behavior instead of failing the whole skeleton.
	FSWGIffChunk RpreChunk, RpstChunk;
	const bool bHasRpre = Reader.FindChildChunk(SktmInner, SWG_IFF_TAG('R','P','R','E'), RpreChunk);
	const bool bHasRpst = Reader.FindChildChunk(SktmInner, SWG_IFF_TAG('R','P','S','T'), RpstChunk);

	FSWGIFFChunkReader InfoReader(InfoChunk, Reader);
	const int32 JointCount = InfoReader.ReadValueLE<int32>();
	if (JointCount <= 0) return false;

	FSWGIFFChunkReader NamesReader(NameChunk, Reader);
	const TArray<FString> Names = NamesReader.ReadTerminatedStrings(JointCount);
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

	FSWGIFFChunkReader PrntReader(PrntChunk, Reader);
	FSWGIFFChunkReader BptrReader(BptrChunk, Reader);
	FSWGIFFChunkReader BproReader(BproChunk, Reader);
	FSWGIFFChunkReader RpreReader(RpreChunk, Reader);
	FSWGIFFChunkReader RpstReader(RpstChunk, Reader);

	OutSkeleton.Joints.Reserve(JointCount);
	for (int32 i = 0; i < JointCount; ++i)
	{
		FSWGSkeletonJoint Joint;
		Joint.Name = Names[i];
		PrntReader.ReadValueLE<int32>(Joint.ParentIndex);
		BptrReader.ReadVectorLE<FVector, float>(Joint.BindPoseTranslation, SWGWorldScale);
		BproReader.ReadQuatLE<FQuat, float>(Joint.BindPoseRotation);
		if (bHasRpre && RpreReader.IsValid())
		{
			RpreReader.ReadQuatLE<FQuat, float>(Joint.PreRotation);
		}
		if (bHasRpst && RpstReader.IsValid())
		{
			RpstReader.ReadQuatLE<FQuat, float>(Joint.PostRotation);
		}

		
		OutSkeleton.Joints.Add(MoveTemp(Joint));

	}

	return true;
}
