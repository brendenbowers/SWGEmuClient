#include "Import/SWGSkeletalMeshImporter.h"

#if WITH_EDITOR

#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "ImportUtils/SkeletalMeshImportUtils.h"
#include "IMeshBuilderModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"

bool FSWGSkeletalMeshImporter::PopulateImportData(
	const FSWGSkeletonData& Skeleton,
	const TArray<const FSWGMeshData*>& MeshParts,
	FSkeletalMeshImportData& OutImportData,
	TArray<FString>& OutMaterialSlotNames)
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
	// and in FSWGRuntimeAnimationPlayer::ApplyPose, or animated poses fight
	// the inverse-bind matrices derived from this reference skeleton.
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

	int32 GlobalMaterialIndex = 0;
	for (const FSWGMeshData* MeshPart : MeshParts)
	{
		if (!MeshPart) continue;

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

		for (const FSWGMeshSubmesh& Submesh : MeshPart->Submeshes)
		{
			const int32 MatIndex = GlobalMaterialIndex++;
			OutMaterialSlotNames.Add(Submesh.ShaderName);

			// Points and Wedges grow 1:1 in this loop — every corner becomes
			// its own Point rather than deduping shared positions across UV
			// seams, trading some memory for a much simpler mapping (skin
			// weights still resolve correctly per corner either way).
			const int32 BaseIndex = OutImportData.Points.Num();
			check(BaseIndex == OutImportData.Wedges.Num());

			for (const FSWGMeshVertex& Vertex : Submesh.Vertices)
			{
				OutImportData.Points.Add(FVector3f(Vertex.Position));

				SkeletalMeshImportData::FVertex Wedge;
				Wedge.VertexIndex = OutImportData.Points.Num() - 1;
				if (Vertex.UVs.Num() > 0)
				{
					Wedge.UVs[0] = FVector2f(Vertex.UVs[0]);
				}
				Wedge.Color = Vertex.bHasColor ? Vertex.Color : FColor::White;
				Wedge.MatIndex = (uint8)MatIndex;
				OutImportData.Wedges.Add(Wedge);

				for (const FSWGBoneWeight& BoneWeight : Vertex.BoneWeights)
				{
					if (!LocalToSkeletonBoneIndex.IsValidIndex(BoneWeight.BoneIndex)) continue;
					const int32 SkeletonBoneIndex = LocalToSkeletonBoneIndex[BoneWeight.BoneIndex];
					if (SkeletonBoneIndex == INDEX_NONE) continue;

					SkeletalMeshImportData::FRawBoneInfluence Influence;
					Influence.VertexIndex = Wedge.VertexIndex;
					Influence.BoneIndex = SkeletonBoneIndex;
					Influence.Weight = BoneWeight.Weight;
					OutImportData.Influences.Add(Influence);
				}
			}

			const int32 TriCount = Submesh.Triangles.Num() / 3;
			for (int32 t = 0; t < TriCount; ++t)
			{
				SkeletalMeshImportData::FTriangle Triangle;
				Triangle.MatIndex = (uint16)MatIndex;
				for (int32 c = 0; c < 3; ++c)
				{
					const int32 CornerLocalIndex = Submesh.Triangles[t * 3 + c];
					const int32 WedgeIndex = BaseIndex + CornerLocalIndex;
					Triangle.WedgeIndex[c] = WedgeIndex;
					// Only normals are decoded from the source file — leaving
					// TangentX/Y zeroed and bHasTangents false below lets the
					// mesh builder derive a tangent basis from these normals
					// + the UVs already supplied, same as FBX imports that
					// don't carry explicit tangents.
					Triangle.TangentZ[c] = FVector3f(Submesh.Vertices[CornerLocalIndex].Normal);
				}
				OutImportData.Faces.Add(Triangle);
			}
		}
	}

	OutImportData.bHasNormals = true;
	OutImportData.bHasTangents = false;
	OutImportData.bHasVertexColors = false;
	OutImportData.NumTexCoords = 1;
	OutImportData.MaxMaterialIndex = FMath::Max(0, GlobalMaterialIndex - 1);

	return OutImportData.Points.Num() > 0 && OutImportData.Faces.Num() > 0;
}

USkeletalMesh* FSWGSkeletalMeshImporter::BuildSkeletalMesh(
	const FSWGSkeletonData& Skeleton,
	const TArray<const FSWGMeshData*>& MeshParts,
	const FString& PackagePath)
{
	FSkeletalMeshImportData ImportData;
	TArray<FString> MaterialSlotNames;
	if (!PopulateImportData(Skeleton, MeshParts, ImportData, MaterialSlotNames))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: no geometry/skeleton to build from for '%s'"), *PackagePath);
		return nullptr;
	}

	const FString AssetName = FPackageName::GetShortName(PackagePath);
	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();

	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(Package, FName(*AssetName), RF_Public | RF_Standalone);

	for (const FString& SlotName : MaterialSlotNames)
	{
		SkeletalMesh->GetMaterials().Add(FSkeletalMaterial(nullptr, true, false, FName(*SlotName), FName(*SlotName)));
	}

	FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	RefSkeleton.Empty();
	{
		FReferenceSkeletonModifier RefSkeletonModifier(RefSkeleton, nullptr);
		for (int32 i = 0; i < Skeleton.Joints.Num(); ++i)
		{
			const FSWGSkeletonJoint& Joint = Skeleton.Joints[i];
			const FMeshBoneInfo BoneInfo(FName(*Joint.Name), Joint.Name, Joint.ParentIndex);
			const FTransform BoneTransform(Joint.ComposeLocalRotation(Joint.BindPoseRotation), Joint.BindPoseTranslation);
			RefSkeletonModifier.Add(BoneInfo, BoneTransform);
		}
	}

	int32 SkeletalDepth = 0;
	if (!SkeletalMeshImportUtils::ProcessImportMeshSkeleton(nullptr, RefSkeleton, SkeletalDepth, ImportData))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: ProcessImportMeshSkeleton failed for '%s'"), *PackagePath);
		return nullptr;
	}
	SkeletalMeshImportUtils::ProcessImportMeshInfluences(ImportData, PackagePath);

	// AddLODInfo() only adds the LOD *metadata* (FSkeletalMeshLODInfo) — the
	// actual geometry slot the mesh builder writes into
	// (FSkeletalMeshModel::LODModels) needs to be added separately, or
	// BuildSkeletalMesh asserts with "LODModels.IsValidIndex(LODIndex)"
	// (hit this on the first real run).
	SkeletalMesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());

	FSkeletalMeshLODInfo& LODInfo = SkeletalMesh->AddLODInfo();
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

	SkeletalMesh->SaveLODImportedData(0, ImportData);

	FBox Bounds(ForceInit);
	for (const FVector3f& Point : ImportData.Points)
	{
		Bounds += FVector(Point);
	}
	SkeletalMesh->SetImportedBounds(FBoxSphereBounds(Bounds));

	SkeletalMesh->AllocateResourceForRendering();

	IMeshBuilderModule& MeshBuilderModule = IMeshBuilderModule::GetForRunningPlatform();
	const FSkeletalMeshBuildParameters BuildParams(SkeletalMesh, GetTargetPlatformManagerRef().GetRunningTargetPlatform(), 0, false);
	if (!MeshBuilderModule.BuildSkeletalMesh(*SkeletalMesh->GetResourceForRendering(), BuildParams))
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: IMeshBuilderModule::BuildSkeletalMesh failed for '%s'"), *PackagePath);
		SkeletalMesh->ReleaseResources();
		return nullptr;
	}
	SkeletalMesh->ReleaseResources();
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

	UE_LOG(LogTemp, Warning, TEXT("FSWGSkeletalMeshImporter: built '%s' — %d bone(s), %d material slot(s), %d point(s), %d face(s)"),
		*PackagePath, RefSkeleton.GetNum(), MaterialSlotNames.Num(), ImportData.Points.Num(), ImportData.Faces.Num());

	return SkeletalMesh;
}

#endif // WITH_EDITOR
