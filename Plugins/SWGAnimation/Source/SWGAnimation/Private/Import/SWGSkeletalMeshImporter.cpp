#include "Import/SWGSkeletalMeshImporter.h"

#if WITH_EDITOR

#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/MorphTargetVertexInfoBuffers.h"
#include "RenderResource.h"
#include "RHIGlobals.h"
#include "Animation/MorphTarget.h"
#include "SkinnedAssetCompiler.h"
#include "ImportUtils/SkeletalMeshImportUtils.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MeshUtilities.h"
#include "LODUtilities.h"

bool FSWGSkeletalMeshImporter::PopulateImportData(
	const FSWGSkeletonData& Skeleton,
	const TArray<const FSWGMeshData*>& MeshParts,
	const FString& PackagePath,
	FSkeletalMeshImportData& OutImportData,
	TArray<FString>& OutMaterialSlotNames,
	TMap<FString, TMap<int32, FVector>>& OutMergedMorphDeltas)
{
	if (Skeleton.Joints.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: skeleton has no joints"));
		return false;
	}

	// Bones — FSWGSkeletonJoint's bind pose is already parent-relative (see
	// its header), matching FSkeletalMeshImportData::FBone's own convention
	// directly. RPRE/RPST are part of the joint's actual local rotation even
	// at bind (see FSWGSkeletonJoint) and must be composed identically here
	// and in FSWGAnimationImporter::BuildAnimSequence, or animated poses
	// fight the inverse-bind matrices derived from this reference skeleton.
	OutImportData.RefBonesBinary.Reserve(Skeleton.Joints.Num());
	for (const FSWGSkeletonJoint& Joint : Skeleton.Joints)
	{
		SkeletalMeshImportData::FBone Bone;
		Bone.Name = Joint.Name;
		Bone.ParentIndex = Joint.ParentIndex;
		Bone.BonePos.Transform = FTransform3f(FQuat4f(Joint.ComposeLocalRotation(Joint.BindPoseRotation)), FVector3f(Joint.BindPoseTranslation));
		OutImportData.RefBonesBinary.Add(Bone);
	}
	for (int32 i = 0; i < OutImportData.RefBonesBinary.Num(); ++i)
	{
		const int32 ParentIndex = OutImportData.RefBonesBinary[i].ParentIndex;
		if (ParentIndex != INDEX_NONE)
		{
			OutImportData.RefBonesBinary[ParentIndex].NumChildren++;
		}
	}

	// Case-insensitive lookup since mesh XFNM bone names are lowercase
	// (e.g. "lthigh") but skeleton joint names are mixed case (e.g. "lThigh")
	// — see FSWGMeshData::BoneNames' comment.
	TMap<FString, int32> JointNameToIndex;
	JointNameToIndex.Reserve(Skeleton.Joints.Num());
	for (int32 i = 0; i < Skeleton.Joints.Num(); ++i)
	{
		JointNameToIndex.Add(Skeleton.Joints[i].Name.ToLower(), i);
	}

	// Facial-expression bones (jaw, lbrow1, reye, lsmile, etc. — see the
	// head .mgn's own local bone list) have no counterpart in the body
	// skeleton at all (a separate wke_m_face.skt sub-skeleton would be
	// needed to rig them properly, not currently wired in). Vertices
	// weighted to them would otherwise silently lose that influence
	// entirely and end up with no bone driving them at all — visually, the
	// face staying frozen in place while the head/neck move away from it.
	// Falling back to "head" instead means those vertices at least move
	// rigidly with the head, which is a reasonable stand-in until a real
	// facial rig exists.
	const int32* HeadJointIndex = JointNameToIndex.Find(TEXT("head"));

	// Blend/morph target name -> (final Points index -> position delta),
	// accumulated across every part/submesh as OutImportData.Points is built
	// below (each part's own MorphTargets are keyed by that part's local
	// POSN index — FSWGMeshVertex::SourcePositionIndex — not the final
	// merged Points index, so this has to be built alongside the main loop,
	// not looked up independently afterward). BuildMorphTargets (called
	// after the actual mesh build — see its header comment) turns this into
	// real UMorphTarget assets; OutImportData itself carries no morph data.
	TMap<FString, TMap<int32, FVector>>& MergedMorphDeltas = OutMergedMorphDeltas;

	int32 GlobalMaterialIndex = 0;
	int32 PartIndex = INDEX_NONE;
	for (const FSWGMeshData* MeshPart : MeshParts)
	{
		++PartIndex;
		if (!MeshPart) continue;

		// A part whose weights can't be resolved contributes no influences at
		// all, and the mesh builder culls influence-less vertices — so the
		// part silently disappears from the merged mesh (bodies with no head,
		// characters that are only hands, etc.). The per-name warning below
		// only covers names that exist but don't match; an absent XFNM chunk
		// leaves BoneNames empty and never reaches it, so call that out here.
		if (MeshPart->BoneNames.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: mesh part %d of '%s' has no XFNM bone names — its vertices fall back to rigid root binding"), PartIndex, *PackagePath);
		}

		// Resolve this mesh part's own XFNM-local bone indices to skeleton
		// joint indices once, rather than re-searching per vertex.
		TArray<int32> LocalToSkeletonBoneIndex;
		LocalToSkeletonBoneIndex.Reserve(MeshPart->BoneNames.Num());
		for (const FString& LocalBoneName : MeshPart->BoneNames)
		{
			const int32* Found = JointNameToIndex.Find(LocalBoneName.ToLower());
			if (Found)
			{
				LocalToSkeletonBoneIndex.Add(*Found);
			}
			else if (HeadJointIndex)
			{
				UE_LOG(LogTemp, Verbose, TEXT("FSWGSkeletalMeshImporter: bone '%s' not found in skeleton — falling back to 'head' (see facial-bone comment)"), *LocalBoneName);
				LocalToSkeletonBoneIndex.Add(*HeadJointIndex);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: bone '%s' not found in skeleton (and no 'head' fallback either) — vertices weighted to it will lose that influence"), *LocalBoneName);
				LocalToSkeletonBoneIndex.Add(INDEX_NONE);
			}
		}

		int32 PartVertexCount = 0;
		int32 PartFallbackVertexCount = 0;

		for (const FSWGMeshSubmesh& Submesh : MeshPart->Submeshes)
		{
			const int32 MatIndex = GlobalMaterialIndex++;
			OutMaterialSlotNames.Add(Submesh.ShaderName);
			// MeshBuilder validates/remaps every face's MatIndex against this
			// raw-import material table. Leaving it empty silently collapses all
			// sections to material zero, even though the triangles themselves
			// carry distinct material IDs.
			SkeletalMeshImportData::FMaterial ImportMaterial;
			ImportMaterial.MaterialImportName = Submesh.ShaderName;
			OutImportData.Materials.Add(MoveTemp(ImportMaterial));

			// Points, Influences, and morph deltas are indexed/deduplicated
			// here — one entry per Submesh.Vertices entry, matching how
			// Submesh.Triangles actually references them (a real indexed
			// triangle list: FSWGMeshReader::ReadSkeletalMeshBindPose reads
			// raw OITL/ITL indices, which repeat whenever a vertex is shared
			// between adjacent triangles — the normal case for any
			// continuous mesh surface).
			//
			// Wedges, though, must NOT be shared this way: IMeshUtilities::
			// BuildSkeletalMesh requires Wedges.Num() >= 3 * Faces.Num()
			// (Skeletal_FindOverlappingCorners' own
			// "check(NumFaces * 3 <= NumWedges)") — every face owns its own
			// 3 wedges even when it shares an underlying point with another
			// face. Building one Wedge per Submesh.Vertices entry (as this
			// used to) instead of one per triangle corner violates that
			// whenever a mesh has any shared/indexed vertices at all, which
			// is virtually always — hence Wedges are built in the triangle
			// loop below instead, one per corner, referencing the shared
			// Point via VertexIndex.
			const int32 PointBaseIndex = OutImportData.Points.Num();

			for (const FSWGMeshVertex& Vertex : Submesh.Vertices)
			{
				OutImportData.Points.Add(FVector3f(Vertex.Position));
				const int32 PointIndex = OutImportData.Points.Num() - 1;
				// PointToRawMap ("current point index -> original import point
				// index") is never populated otherwise, which leaves it
				// default-empty — CopyLODImportData copies that straight
				// through as PointToOriginalMap, and FMeshUtilities::
				// BuildSkeletalModelFromChunks indexes it unconditionally
				// (MeshUtilities.cpp:945), crashing on any real mesh. No
				// separate dedup/reorder pass happens here, so the mapping
				// is simply identity.
				OutImportData.PointToRawMap.Add(PointIndex);

				if (Vertex.SourcePositionIndex != INDEX_NONE)
				{
					for (const FSWGMeshMorphTarget& MorphTarget : MeshPart->MorphTargets)
					{
						if (const FVector* Delta = MorphTarget.PositionDeltas.Find(Vertex.SourcePositionIndex))
						{
							MergedMorphDeltas.FindOrAdd(MorphTarget.Name).Add(PointIndex, *Delta);
						}
					}
				}

				int32 VertexInfluenceCount = 0;
				for (const FSWGBoneWeight& BoneWeight : Vertex.BoneWeights)
				{
					if (!LocalToSkeletonBoneIndex.IsValidIndex(BoneWeight.BoneIndex)) continue;
					const int32 SkeletonBoneIndex = LocalToSkeletonBoneIndex[BoneWeight.BoneIndex];
					if (SkeletonBoneIndex == INDEX_NONE) continue;

					SkeletalMeshImportData::FRawBoneInfluence Influence;
					Influence.VertexIndex = PointIndex;
					Influence.BoneIndex = SkeletonBoneIndex;
					Influence.Weight = BoneWeight.Weight;
					OutImportData.Influences.Add(Influence);
					++VertexInfluenceCount;
				}

				// The mesh builder culls vertices that carry no influence at
				// all, which silently deletes whole body parts rather than
				// failing — bind those rigidly to the root instead. The
				// geometry is already in bind pose, so it lands in the right
				// place and follows the character; it just won't deform.
				if (VertexInfluenceCount == 0)
				{
					SkeletalMeshImportData::FRawBoneInfluence Influence;
					Influence.VertexIndex = PointIndex;
					Influence.BoneIndex = 0;
					Influence.Weight = 1.0f;
					OutImportData.Influences.Add(Influence);
					++PartFallbackVertexCount;
				}
				++PartVertexCount;
			}

			const int32 TriCount = Submesh.Triangles.Num() / 3;
			for (int32 t = 0; t < TriCount; ++t)
			{
				SkeletalMeshImportData::FTriangle Triangle;
				Triangle.MatIndex = (uint16)MatIndex;
				for (int32 c = 0; c < 3; ++c)
				{
					const int32 CornerLocalIndex = Submesh.Triangles[t * 3 + c];
					const FSWGMeshVertex& CornerVertex = Submesh.Vertices[CornerLocalIndex];
					const int32 PointIndex = PointBaseIndex + CornerLocalIndex;

					// A fresh Wedge per triangle corner, even when it shares
					// an underlying Point with another face's corner — see
					// the comment above OutImportData.Points.Num() for why.
					SkeletalMeshImportData::FVertex Wedge;
					Wedge.VertexIndex = PointIndex;
					if (CornerVertex.UVs.Num() > 0)
					{
						Wedge.UVs[0] = FVector2f(CornerVertex.UVs[0]);
					}
					Wedge.Color = CornerVertex.bHasColor ? CornerVertex.Color : FColor::White;
					Wedge.MatIndex = (uint8)MatIndex;
					const int32 WedgeIndex = OutImportData.Wedges.Add(Wedge);

					Triangle.WedgeIndex[c] = WedgeIndex;
					// Only normals are decoded from the source file — leaving
					// TangentX/Y zeroed and bHasTangents false below lets the
					// mesh builder derive a tangent basis from these normals
					// + the UVs already supplied, same as FBX imports that
					// don't carry explicit tangents.
					Triangle.TangentZ[c] = FVector3f(CornerVertex.Normal);
				}
				OutImportData.Faces.Add(Triangle);
			}
		}

		if (PartFallbackVertexCount > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: mesh part %d of '%s' — %d of %d vertices had no resolvable bone weights and were rigidly bound to the root; this part won't deform with the skeleton"),
				PartIndex, *PackagePath, PartFallbackVertexCount, PartVertexCount);
		}
	}

	OutImportData.bHasNormals = true;
	OutImportData.bHasTangents = false;
	// SWG's two-tone creature coloring (e.g. a Wookiee's light/dark fur mix,
	// or an alien's body-vs-limb color split) is driven by per-vertex color
	// blending between two customization-palette tints (index_color_1 and
	// index_color_3) — the .mgn's own vertex color IS that per-vertex blend
	// mask (see Wedge.Color above, already correctly decoded from
	// FSWGMeshVertex::Color/bHasColor). This used to be thrown away here
	// before that mechanism was understood; M_SWGObjectTextured/Masked now
	// consume it via a VertexColor-driven Lerp between TintColor/TintColor2.
	OutImportData.bHasVertexColors = true;
	OutImportData.NumTexCoords = 1;
	OutImportData.MaxMaterialIndex = FMath::Max(0, GlobalMaterialIndex - 1);

	return OutImportData.Points.Num() > 0 && OutImportData.Faces.Num() > 0;
}

bool FSWGSkeletalMeshImporter::BuildSkeletalMeshData(IMeshUtilities& MeshUtilities, const FSWGSkeletonData& Skeleton, const TArray<const FSWGMeshData*>& MeshParts, const TMap<FString, FString>& SlotHardpoints, const FString& PackagePath, FSWGSkeletalMeshBuildData& OutData)
{
	OutData.LODModel = MakeUnique<FSkeletalMeshLODModel>();
	TArray<FString> MaterialSlotNames;
	TMap<FString, TMap<int32, FVector>> MergedMorphDeltas;
	if (!PopulateImportData(Skeleton, MeshParts, PackagePath, OutData.ImportData, MaterialSlotNames, MergedMorphDeltas))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: no geometry/skeleton to build from for '%s'"), *PackagePath);
		return false;
	}


	for (const FString& SlotName : MaterialSlotNames)
	{
		OutData.Materials.Add(FSkeletalMaterial(nullptr, true, false, FName(*SlotName), FName(*SlotName)));
	}

	OutData.RefSkeleton.Empty();
	{
		FReferenceSkeletonModifier RefSkeletonModifier(OutData.RefSkeleton, nullptr);
		for (int32 i = 0; i < Skeleton.Joints.Num(); ++i)
		{
			const FSWGSkeletonJoint& Joint = Skeleton.Joints[i];
			const FMeshBoneInfo BoneInfo(FName(*Joint.Name), Joint.Name, Joint.ParentIndex);
			const FTransform BoneTransform(Joint.ComposeLocalRotation(Joint.BindPoseRotation), Joint.BindPoseTranslation);
			RefSkeletonModifier.Add(BoneInfo, BoneTransform);
		}
	}

	TSet<FName> AddedSocketNames;
	for (const TPair<FString, FString>& Slot : SlotHardpoints)
	{
		const int32 BoneIndex = OutData.RefSkeleton.FindBoneIndex(FName(*Slot.Value));
		if (BoneIndex == INDEX_NONE)
		{
			continue;
		}

		OutData.Sockets.Add({ 
			FName(*Slot.Key), 
			OutData.RefSkeleton.GetBoneName(BoneIndex), 
			FVector::ZeroVector, 
			FRotator::ZeroRotator 
		});
		AddedSocketNames.Add(FName(*Slot.Key));
	}

	for (const FSWGMeshData* MeshPart : MeshParts)
	{
		if (!MeshPart)
		{
			continue;
		}
		for (const FSWGMeshHardpoint& Hardpoint : MeshPart->Hardpoints)
		{
			const int32 ParentIndex = OutData.RefSkeleton.FindBoneIndex(FName(*Hardpoint.ParentName));
			const FName SocketName(*Hardpoint.Name);
			if (ParentIndex == INDEX_NONE || AddedSocketNames.Contains(SocketName))
			{
				continue;
			}

			OutData.Sockets.Add({
				SocketName,
				OutData.RefSkeleton.GetBoneName(ParentIndex),
				Hardpoint.Translation,
				Hardpoint.Rotation.Rotator()
			});
			AddedSocketNames.Add(SocketName);
		}
	}

	int32 SkeletalDepth = 0;
	if (!SkeletalMeshImportUtils::ProcessImportMeshSkeleton(nullptr, OutData.RefSkeleton, SkeletalDepth, OutData.ImportData))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: ProcessImportMeshSkeleton failed for '%s'"), *PackagePath);
		return false;
	}
	SkeletalMeshImportUtils::ProcessImportMeshInfluences(OutData.ImportData, PackagePath);


	FSkeletalMeshLODInfo& LODInfo = OutData.LODInfo;
	LODInfo.LODMaterialMap.SetNum(MaterialSlotNames.Num());
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialSlotNames.Num(); ++MaterialIndex)
	{
		LODInfo.LODMaterialMap[MaterialIndex] = MaterialIndex;
	}
	LODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
	LODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
	LODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
	LODInfo.LODHysteresis = 0.02f;
	LODInfo.BuildSettings.bRecomputeNormals = false;
	LODInfo.BuildSettings.bRecomputeTangents = true;
	LODInfo.BuildSettings.bUseMikkTSpace = true;
	LODInfo.BuildSettings.ThresholdPosition = 0.02f;
	LODInfo.BuildSettings.ThresholdTangentNormal = 0.02f;
	LODInfo.BuildSettings.ThresholdUV = 0.001f;


	FBox Bounds(ForceInit);
	for (const FVector3f& Point : OutData.ImportData.Points)
	{
		Bounds += FVector(Point);
	}
	OutData.Bounds = FBoxSphereBounds(Bounds);


	TArray<FVector3f> LODPoints;
	TArray<SkeletalMeshImportData::FMeshWedge> LODWedges;
	TArray<SkeletalMeshImportData::FMeshFace> LODFaces;
	TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
	TArray<int32> LODPointToRawMap;
	OutData.ImportData.CopyLODImportData(LODPoints, LODWedges, LODFaces, LODInfluences, LODPointToRawMap);

	FLODUtilities::AdjustImportDataFaceMaterialIndex(OutData.Materials, OutData.ImportData.Materials, LODFaces, /*LODIndex*/ 0);
	
	IMeshUtilities::MeshBuildOptions Options;
	Options.FillOptions(OutData.LODInfo.BuildSettings);   // BuildSettings already populated per the LODInfo answer above
	Options.bComputeNormals |= !OutData.ImportData.bHasNormals;
	Options.bComputeTangents |= !OutData.ImportData.bHasTangents;

	if (!MeshUtilities.BuildSkeletalMesh(
		*OutData.LODModel,
		PackagePath,          // used only for logging inside
		OutData.RefSkeleton,
		LODInfluences,
		LODWedges,
		LODFaces,
		LODPoints,
		LODPointToRawMap,
		Options))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: MeshUtilities.BuildSkeletalMesh failed for '%s'"), *PackagePath);
		return false;
	}
	OutData.LODModel->NumTexCoords = FMath::Max<int32>(1, OutData.ImportData.NumTexCoords);


	return true;
}

USkeletalMesh* FSWGSkeletalMeshImporter::FinalizeSkeletalMesh(FSWGSkeletalMeshBuildData&& Data, const FString& PackagePath)
{
	const FString AssetName = FPackageName::GetShortName(PackagePath);
	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();

	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(Package, FName(*AssetName), RF_Public | RF_Standalone);


	SkeletalMesh->GetRefSkeleton() = MoveTemp(Data.RefSkeleton);
	SkeletalMesh->GetMaterials() = MoveTemp(Data.Materials);

	for (const FSWGSocketDesc& SlotDesc : Data.Sockets)
	{
		USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(SkeletalMesh);
		Socket->SocketName = SlotDesc.SocketName;
		Socket->BoneName = SlotDesc.BoneName;
		Socket->RelativeLocation = SlotDesc.RelativeLocation;
		Socket->RelativeRotation = SlotDesc.RelativeRotation;
		SkeletalMesh->GetMeshOnlySocketList().Add(Socket);
	}

	SkeletalMesh->AddLODInfo() = MoveTemp(Data.LODInfo);
	SkeletalMesh->GetImportedModel()->LODModels.Add(Data.LODModel.Release());
	SkeletalMesh->SaveLODImportedData(0, Data.ImportData);
	SkeletalMesh->SetHasVertexColors(true);
	SkeletalMesh->SetVertexColorGuid(FGuid::NewGuid());

	SkeletalMesh->SetImportedBounds(Data.Bounds);

	SkeletalMesh->AllocateResourceForRendering();

	SkeletalMesh->CalculateInvRefMatrices();

	// A dedicated USkeleton per built mesh, rather than trying to share/reuse
	// one across every creature — simplest correct option for now, matching
	// this milestone's "just get one Wookiee posable" scope; sharing one
	// skeleton asset across every biped is a natural follow-up once more
	// than one creature is imported this way.
	UPackage* SkeletonPackage = CreatePackage(*(PackagePath + TEXT("_Skeleton")));
	SkeletonPackage->FullyLoad();
	USkeleton* NewSkeleton = NewObject<USkeleton>(SkeletonPackage, FName(*(AssetName + TEXT("_Skeleton"))), RF_Public | RF_Standalone);
	if (!NewSkeleton->MergeAllBonesToBoneTree(SkeletalMesh))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: MergeAllBonesToBoneTree failed for '%s'"), *PackagePath);
		return nullptr;
	}
	SkeletalMesh->SetSkeleton(NewSkeleton);

	SkeletalMesh->MarkPackageDirty();
	SkeletalMesh->PostEditChange();
	NewSkeleton->MarkPackageDirty();

	// After PostEditChange, not before: PostEditChange on a freshly-built
	// mesh can trigger an internal rebuild from the raw import data saved
	// via SaveLODImportedData above — which carries no morph data (this
	// importer never populates FSkeletalMeshImportData::MorphTargets; see
	// BuildMorphTargets' own header comment for why) — and that rebuild was
	// silently wiping out any morph targets registered before it ran. That
	// rebuild also runs asynchronously (FSkinnedAssetCompilingManager), so
	// even running after PostEditChange returns isn't enough on its own —
	// force it to finish first, or it can still race BuildMorphTargets and
	// win, wiping the morph targets out from under it.
	FSkinnedAssetCompilingManager::Get().FinishCompilation({ SkeletalMesh });
	BuildMorphTargets(*SkeletalMesh, Data.MorphDeltas);
	// BuildMorphTargets' own InitMorphTargetsAndRebuildRenderData can kick off
	// further async work too — flush again before saving, or SavePackage can
	// race it the same way.
	FSkinnedAssetCompilingManager::Get().FinishCompilation({ SkeletalMesh });

	UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: '%s' GetMorphTargets().Num()=%d right before SavePackage"),
		*PackagePath, SkeletalMesh->GetMorphTargets().Num());

	FAssetRegistryModule::AssetCreated(SkeletalMesh);
	FAssetRegistryModule::AssetCreated(NewSkeleton);

	auto SavePackage = [](UPackage* PackageToSave, UObject* Asset)
		{
			const FString FileName = FPackageName::LongPackageNameToFilename(PackageToSave->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			return UPackage::SavePackage(PackageToSave, Asset, *FileName, SaveArgs);
		};
	SavePackage(Package, SkeletalMesh);
	SavePackage(SkeletonPackage, NewSkeleton);

	// Data.RefSkeleton/Data.Materials were moved out above — read the counts
	// back from SkeletalMesh itself rather than the now-empty moved-from Data.
	UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: built '%s' — %d bone(s), %d material slot(s), %d point(s), %d face(s)"),
		*PackagePath, SkeletalMesh->GetRefSkeleton().GetNum(), SkeletalMesh->GetMaterials().Num(), Data.ImportData.Points.Num(), Data.ImportData.Faces.Num());

	return SkeletalMesh;
}

void FSWGSkeletalMeshImporter::BuildMorphTargets(USkeletalMesh& SkeletalMesh, const TMap<FString, TMap<int32, FVector>>& MergedMorphDeltas)
{
	if (MergedMorphDeltas.Num() == 0)
	{
		return;
	}

	const FSkeletalMeshLODModel& LODModel = SkeletalMesh.GetImportedModel()->LODModels[0];

	// Invert MeshToImportVertexMap once (render vertex index -> import point
	// index) into (import point index -> render vertex indices) — a single
	// import point can correspond to more than one render vertex (UV/normal
	// seams split it during build), and every one of them needs the same
	// delta for the shape to look right, not just the first.
	TMap<int32, TArray<int32>> ImportPointToRenderVertices;
	ImportPointToRenderVertices.Reserve(LODModel.MeshToImportVertexMap.Num());
	for (int32 RenderVertexIndex = 0; RenderVertexIndex < LODModel.MeshToImportVertexMap.Num(); ++RenderVertexIndex)
	{
		ImportPointToRenderVertices.FindOrAdd(LODModel.MeshToImportVertexMap[RenderVertexIndex]).Add(RenderVertexIndex);
	}

	int32 NumBuilt = 0;
	for (const TPair<FString, TMap<int32, FVector>>& MorphEntry : MergedMorphDeltas)
	{
		TArray<FMorphTargetDelta> Deltas;
		Deltas.Reserve(MorphEntry.Value.Num());
		for (const TPair<int32, FVector>& DeltaEntry : MorphEntry.Value)
		{
			const TArray<int32>* RenderVertices = ImportPointToRenderVertices.Find(DeltaEntry.Key);
			if (!RenderVertices)
			{
				continue; // This point didn't survive the build (e.g. an unreferenced/culled vertex) — nothing to apply the delta to.
			}
			for (int32 RenderVertexIndex : *RenderVertices)
			{
				FMorphTargetDelta& Delta = Deltas.AddDefaulted_GetRef();
				Delta.SourceIdx = (uint32)RenderVertexIndex;
				Delta.PositionDelta = FVector3f(DeltaEntry.Value);
				// TangentZDelta left zero — normal deltas aren't decoded (see
				// FSWGMeshReader::ReadBlendTargets); the renderer still
				// shades morphed geometry reasonably using the base normals.
			}
		}

		if (Deltas.Num() == 0)
		{
			continue;
		}

		UMorphTarget* MorphTarget = NewObject<UMorphTarget>(&SkeletalMesh, FName(*MorphEntry.Key));
		MorphTarget->BaseSkelMesh = &SkeletalMesh;

		FMorphTargetLODModel MorphLODModel;
		MorphLODModel.Vertices = MoveTemp(Deltas);
		MorphLODModel.NumVertices = MorphLODModel.Vertices.Num();
		MorphLODModel.NumBaseMeshVerts = LODModel.NumVertices;
		for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); ++SectionIndex)
		{
			MorphLODModel.SectionIndices.Add(SectionIndex);
		}
		MorphTarget->GetMorphLODModels().Add(MoveTemp(MorphLODModel));

		SkeletalMesh.RegisterMorphTarget(MorphTarget, /*bInvalidateRenderData=*/false);
		++NumBuilt;
	}

	UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: registered %d/%d morph target(s) for '%s', GetMorphTargets().Num()=%d before InitMorphTargets"),
		NumBuilt, MergedMorphDeltas.Num(), *SkeletalMesh.GetPathName(), SkeletalMesh.GetMorphTargets().Num());

	// InitMorphTargets() (not the *AndRebuildRenderData variant): confirmed via
	// logging that InitMorphTargetsAndRebuildRenderData's FScopedSkeletalMeshPostEditChange
	// deterministically wipes every morph target just registered above — its
	// destructor calls SkeletalMesh->PostEditChange(), which re-triggers a full
	// rebuild from raw import data (which never carries morph info; this
	// importer only ever populates morph data via RegisterMorphTarget, not
	// FSkeletalMeshImportData::MorphTargets — see the class header comment).
	// InitMorphTargets() alone just rebuilds the ShapeName->index map and
	// prunes empty targets, no PostEditChange involved.
	if (NumBuilt > 0)
	{
		SkeletalMesh.InitMorphTargets();
	}

	// InitMorphTargets() doesn't touch render data, though, and the actual
	// GPU-side compressed morph buffer (LODRenderData[0].MorphTargetVertexInfoBuffers)
	// was already marked "initialized" (just empty — bResourcesInitialized=true,
	// GetNumMorphs()=0) — originally observed with the build going through
	// IMeshBuilderModule::BuildSkeletalMesh (since replaced by the direct
	// FMeshUtilities::BuildSkeletalMesh call in BuildSkeletalMeshData, still
	// running back when no morph targets existed yet); worth re-confirming
	// via logging that the same "already initialized empty" state still
	// holds now that PostEditChange's CacheDerivedData is what actually
	// populates LODRenderData, not this call directly. USkeletalMesh::
	// InitResources() silently no-ops against an already-"initialized"
	// buffer, so calling it again here does nothing (confirmed via logging
	// against the old call path: GetNumMorphs() stayed 0).
	// Left empty, this is harmless right up until a material on the mesh
	// declares bUsedWithMorphTargets, at which point USkinnedMeshComponent's
	// GPU skin cache path hits `ensureAlways(DynamicData->MorphTargetWeights.Num()
	// == LODData.MorphTargetVertexInfoBuffers.GetNumMorphs())` every frame
	// (SkeletalRenderGPUSkin.cpp) since the buffer still reports 0 morphs.
	// InitMorphResources() (the "first build" API) hard-crashes via
	// check(!IsMorphResourcesInitialized()) since the buffer's already marked
	// initialized (empty) from the original build. InitMorphResourcesStreaming()
	// is the sibling API for exactly this situation — a previously-initialized
	// buffer that needs rebuilding (its usual caller is LOD streaming) — and
	// calls the protected ResetCPUData() internally first, so no check() trip.
	if (NumBuilt > 0)
	{
		if (FSkeletalMeshRenderData* RenderData = SkeletalMesh.GetResourceForRendering())
		{
			if (RenderData->LODRenderData.IsValidIndex(0))
			{
				FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
				const FSkeletalMeshLODInfo* LODInfo = SkeletalMesh.GetLODInfo(0);

				TArray<const FMorphTargetLODModel*> MorphTargetLODModels;
				MorphTargetLODModels.Reserve(SkeletalMesh.GetMorphTargets().Num());
				for (const TObjectPtr<UMorphTarget>& MorphTarget : SkeletalMesh.GetMorphTargets())
				{
					const TArray<FMorphTargetLODModel>& LODModels = MorphTarget->GetMorphLODModels();
					if (LODModels.IsValidIndex(0))
					{
						MorphTargetLODModels.Add(&LODModels[0]);
					}
				}

				LODData.MorphTargetVertexInfoBuffers.InitMorphResourcesStreaming(
					GMaxRHIShaderPlatform,
					LODData.RenderSections,
					MorphTargetLODModels,
					LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices(),
					LODInfo ? LODInfo->MorphTargetPositionErrorTolerance : 0.0f);

				BeginInitResource(&LODData.MorphTargetVertexInfoBuffers);
			}
		}
	}

	if (FSkeletalMeshRenderData* RenderData = SkeletalMesh.GetResourceForRendering())
	{
		if (RenderData->LODRenderData.IsValidIndex(0))
		{
			UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: '%s' MorphTargetVertexInfoBuffers.GetNumMorphs()=%d after direct rebuild"),
				*SkeletalMesh.GetPathName(), RenderData->LODRenderData[0].MorphTargetVertexInfoBuffers.GetNumMorphs());
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: built %d/%d morph target(s) for '%s', GetMorphTargets().Num()=%d after InitMorphTargets"),
		NumBuilt, MergedMorphDeltas.Num(), *SkeletalMesh.GetPathName(), SkeletalMesh.GetMorphTargets().Num());
}

#endif // WITH_EDITOR
