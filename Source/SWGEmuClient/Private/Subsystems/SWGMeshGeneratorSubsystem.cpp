#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "TRE/SWGIffTags.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGDDSTextureLoader.h"
#include "TRE/SWGShaderReader.h"
#include "TRE/SWGCustomizationIdManager.h"
#include "TRE/SWGAssetCustomizationManager.h"
#include "TRE/SWGCrc32.h"
#include "TRE/SWGSkeletonReader.h"
#include "TRE/SWGSlotDefinitionReader.h"
#include "TRE/SWGAnimationReader.h"
#include "TRE/SWGIFFChunkReader.h"
#include "Import/SWGSkeletalMeshImporter.h"
#include "Import/SWGAnimationImporter.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "UObject/SoftObjectPath.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Misc/Optional.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGStaticProp.h"
#include "Objects/World/SWGInstallation.h"
#include "Objects/Tangible/SWGItem.h"
#include "Objects/Player/SWGPlayer.h"
#include "Components/SWGMovementComponent.h"
#include "Components/SWGTangibleComponent.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ImageUtils.h"
#include "TextureResource.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/Canvas.h"

using namespace UE::Geometry;

namespace
{
	// Whole-chunk null-terminated ascii string (chunk's only content is the
	// string) — same idiom as FSWGMeshReader::ReadNullTerminatedString.
	FString ReadFullChunkString(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk)
	{
		const uint8* Data = Reader.GetChunkData(Chunk);
		const int32 Len = FMath::Max(0, Chunk.DataSize - 1); // trailing null
		return FString::ConstructFromPtrSize((const ANSICHAR*)Data, Len);
	}

	// Most static MSH files use the common SWG forward-axis convention after
	// ReadVector3LE's Y-up -> Z-up conversion. A small set of kiosk/terminal
	// assets are authored with their local forward axis rotated right by one
	// quarter turn. Keep this at the component level: actor rotation remains
	// the server-authoritative world facing.
	float GetStaticMeshYawCorrection(const TArray<FString>& MeshVirtualPaths)
	{
		return 0.0f;
	}

	bool ReadDefaultLocomotionPaths(const FSWGIffReader& Reader, TArray<FString>& OutPaths)
	{
		TArray<FString> Clips;
		auto Visit = [&Reader, &Clips](auto&& Self, const FSWGIffChunk& Node) -> void
		{
			if (Node.IsForm())
			{
				for (const FSWGIffChunk& Child : Reader.ReadChildren(Node)) Self(Self, Child);
				return;
			}

			FSWGIFFChunkReader ChunkReader(Node, Reader);
			const FString Value = ChunkReader.ReadTerminiatedString();
		
			if (Value.EndsWith(TEXT(".ans"), ESearchCase::IgnoreCase))
			{
				Clips.AddUnique(Value);
			}
		};

		for (const FSWGIffChunk& Root : Reader.ReadChunks()) Visit(Visit, Root);
		auto FindClip = [&Clips](const TCHAR* Needle) -> FString
		{
			const FString* Found = Clips.FindByPredicate([Needle](const FString& Path) { return Path.Contains(Needle, ESearchCase::IgnoreCase); });
			return Found ? *Found : FString();
		};

		const FString Idle = FindClip(TEXT("_idl_breathe_normally."));
		const FString Walk = FindClip(TEXT("_loc_walk"));
		const FString Run = FindClip(TEXT("_loc_run."));
		if (Idle.IsEmpty() || Walk.IsEmpty() || Run.IsEmpty()) return false;

		OutPaths = { Idle, Walk, Walk, Run }; // LAT provides a parametric walk/run pair, not a distinct jog clip.
		return true;
	}

	bool ReadSatLatPaths(const FSWGIffReader& Reader, const FSWGIffChunk& LatxChunk, TMap<FString, FString>& OutPaths)
	{
		const uint8* Data = Reader.GetChunkData(LatxChunk);
		const int32 Size = Reader.GetChunkSize(LatxChunk);
		if (Size < 2) return false;

		const int32 Count = Data[0] | (Data[1] << 8); // LATX count is little-endian.
		int32 Offset = 2;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			auto ReadString = [&Data, Size, &Offset](FString& OutValue) -> bool
			{
				const int32 Start = Offset;
				while (Offset < Size && Data[Offset] != 0) ++Offset;
				if (Offset >= Size) return false;
				OutValue = FString::ConstructFromPtrSize((const ANSICHAR*)(Data + Start), Offset - Start);
				++Offset;
				return !OutValue.IsEmpty();
			};

			FString SkeletonPath, LatPath;
			if (!ReadString(SkeletonPath) || !ReadString(LatPath)) return false;
			OutPaths.Add(MoveTemp(SkeletonPath), MoveTemp(LatPath));
		}
		return !OutPaths.IsEmpty();
	}

	// Finds the one non-DERV FORM child of a SCOT/STOT/SHOT node — the
	// versioned data form (0007/0008/0009/...) holding that layer's XXXX
	// key-value fields (DERV is the base-template reference, a sibling, not
	// the data itself).
	bool FindVersionedDataForm(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, FSWGIffChunk& OutForm)
	{
		for (const FSWGIffChunk& Child : Reader.ReadChildren(Parent))
		{
			if (Child.IsForm() && Child.FormType != SWG_IFF_TAG('D','E','R','V'))
			{
				OutForm = Child;
				return true;
			}
		}
		return false;
	}

	// XXXX key-value layout: key(ascii, null-terminated), then a flag byte
	// (0x00 = default/no value follows, 0x01 = has value), then — for a
	// string-typed field like appearanceFilename — another null-terminated
	// ascii string. (Numeric fields insert an extra type-tag byte before the
	// value; not needed here since we only ever look for this one string key.)
	bool FindXxxxStringValue(const FSWGIffReader& Reader, const FSWGIffChunk& DataForm, const TCHAR* Key, FString& OutValue)
	{
		for (const FSWGIffChunk& Child : Reader.ReadChildren(DataForm))
		{
			if (Child.Tag != SWG_IFF_TAG('X','X','X','X'))
			{
				continue;
			}

			FSWGIFFChunkReader ChunkReader(Child, Reader);

			FString ChunkKey;
			if (!ChunkReader.ReadTerminiatedString(ChunkKey))
			{
				continue;
			}

			// Each XXXX chunk carries a different named property (e.g.
			// "wearableTypeMask", "species", "appearanceFilename") — only
			// the one matching Key is a candidate; every other chunk must
			// be skipped.
			if (!ChunkKey.Equals(Key))
			{
				continue;
			}

			// One byte, not four: a bool "value present" flag immediately
			// following the key's null terminator.
			const uint8 Flag = ChunkReader.ReadValueLE<uint8>();
			if (Flag == 0)
			{
				return false;
			}

			return ChunkReader.ReadTerminiatedString(OutValue) && !OutValue.IsEmpty();
		}

		return false;
	}

	bool FindAppearanceFilename(const FSWGIffReader& Reader, const FSWGIffChunk& DataForm, FString& OutAppearancePath)
	{
		return FindXxxxStringValue(Reader, DataForm, TEXT("appearanceFilename"), OutAppearancePath);
	}

	// Buildings with interiors leave appearanceFilename empty and reference
	// their exterior shell via portalLayoutFilename (.pob) instead. The .pob's
	// CELS form holds one FORM CELL per interior room plus an implicit "cell 0"
	// for the outside; cell 0's meshFile (read positionally, not as an XXXX
	// key/value) is the building's outer shell appearance path.
	bool ReadPobExteriorMeshPath(const FSWGIffReader& Reader, FString& OutAppearancePath)
	{
		FSWGIffChunk PrtoForm;
		if (!Reader.FindForm(SWG_IFF_TAG('P','R','T','O'), PrtoForm))
		{
			return false;
		}

		TArray<FSWGIffChunk> PrtoChildren = Reader.ReadChildren(PrtoForm);
		if (PrtoChildren.Num() == 0 || !PrtoChildren[0].IsForm())
		{
			return false;
		}
		const FSWGIffChunk& VersionForm = PrtoChildren[0]; // "0003" or "0004"

		FSWGIffChunk CelsForm;
		bool bFoundCels = false;
		for (const FSWGIffChunk& Child : Reader.ReadChildren(VersionForm))
		{
			if (Child.IsForm() && Child.FormType == SWG_IFF_TAG('C','E','L','S'))
			{
				CelsForm = Child;
				bFoundCels = true;
				break;
			}
		}
		if (!bFoundCels)
		{
			return false;
		}

		TArray<FSWGIffChunk> CellForms = Reader.ReadChildren(CelsForm);
		if (CellForms.Num() == 0 || !CellForms[0].IsForm() || CellForms[0].FormType != SWG_IFF_TAG('C','E','L','L'))
		{
			return false;
		}
		const FSWGIffChunk& OutsideCell = CellForms[0]; // cell index 0 == the outside/world cell

		TArray<FSWGIffChunk> CellVersionForms = Reader.ReadChildren(OutsideCell);
		if (CellVersionForms.Num() == 0 || !CellVersionForms[0].IsForm())
		{
			return false;
		}
		const FSWGIffChunk& CellVersionForm = CellVersionForms[0]; // "0004" or "0005"

		FSWGIffChunk DataChunk;
		bool bFoundData = false;
		for (const FSWGIffChunk& Child : Reader.ReadChildren(CellVersionForm))
		{
			if (Child.Tag == SWGIffTags::Data)
			{
				DataChunk = Child;
				bFoundData = true;
				break;
			}
		}
		if (!bFoundData)
		{
			return false;
		}

		const uint8* D = Reader.GetChunkData(DataChunk);
		const int32 Size = Reader.GetChunkSize(DataChunk);

		// numberOfPortals(int32) + unk(byte), then two null-terminated strings:
		// name, meshFile. We don't need numberOfPortals' actual value, just to
		// skip past it and the flag byte.
		int32 Offset = 5;
		if (Offset >= Size)
		{
			return false;
		}

		while (Offset < Size && D[Offset] != 0) { ++Offset; } // skip name
		++Offset; // skip its null terminator

		if (Offset >= Size)
		{
			return false;
		}

		const int32 MeshStart = Offset;
		while (Offset < Size && D[Offset] != 0) { ++Offset; }

		OutAppearancePath = FString::ConstructFromPtrSize((const ANSICHAR*)(D + MeshStart), Offset - MeshStart);
		return !OutAppearancePath.IsEmpty();
	}

	// A FORM DERV child of SHOT (when present) is the base-template reference:
	// FORM DERV > CHUNK XXXX holding a single null-terminated path string (not
	// the key/flag/value layout parseVariableData uses). Buildings' own .iff
	// usually has an empty appearanceFilename since it's inherited from this
	// base template rather than redefined per-building.
	bool FindDervParentPath(const FSWGIffReader& Reader, const FSWGIffChunk& ShotForm, FString& OutParentPath)
	{
		for (const FSWGIffChunk& Child : Reader.ReadChildren(ShotForm))
		{
			if (Child.IsForm() && Child.FormType == SWG_IFF_TAG('D','E','R','V'))
			{
				for (const FSWGIffChunk& DervChild : Reader.ReadChildren(Child))
				{
					if (DervChild.Tag == SWG_IFF_TAG('X','X','X','X'))
					{
						OutParentPath = ReadFullChunkString(Reader, DervChild);
						return !OutParentPath.IsEmpty();
					}
				}
			}
		}
		return false;
	}

	// Diagnostic: buildings have no "appearanceFilename" XXXX entry at all —
	// dump every XXXX key actually present so we can find whatever field
	// holds their exterior shell reference instead (getPortalLayout() in
	// Core3's own SharedBuildingObjectTemplate is a separate, larger
	// structure — not what we want; we only need the outer model for now).
	void DumpAllXxxxKeys(const FSWGIffReader& Reader, const FSWGIffChunk& DataForm, const FString& TemplatePath)
	{
		for (const FSWGIffChunk& Child : Reader.ReadChildren(DataForm))
		{
			if (Child.Tag != SWG_IFF_TAG('X','X','X','X'))
				continue;

			const uint8* Data = Reader.GetChunkData(Child);
			const int32 Size = Reader.GetChunkSize(Child);

			int32 KeyEnd = 0;
			while (KeyEnd < Size && Data[KeyEnd] != 0) { ++KeyEnd; }

			const FString Key = FString::ConstructFromPtrSize((const ANSICHAR*)Data, KeyEnd);
			UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: %s XXXX key: %s"), *TemplatePath, *Key);
		}
	}

	// Picks the highest-detail entry from a LOD candidate list by filename
	// convention ("..._l0.msh"/"..._l0.mgn" — confirmed against real files),
	// rather than relying on any numeric index/order, which isn't consistent
	// between the .lod (CHLD, index doesn't match LOD number) and .lmg (NAME,
	// no index at all) formats. Falls back to the first entry if no "_l0"
	// match is found (e.g. a prop with only one mesh, no LOD chain).
	FString PickHighestDetailLod(const TArray<FString>& Candidates)
	{
		for (const FString& Candidate : Candidates)
		{
			if (Candidate.Contains(TEXT("_l0.")))
			{
				return Candidate;
			}
		}
		return Candidates.Num() > 0 ? Candidates[0] : FString();
	}

	// Placeholder-material debugging aid: every SWG object still renders with
	// the same flat default material, making it hard to tell what's clipping
	// into what (terrain vs. buildings, props vs. buildings) on screen,
	// especially with shadows off. Tint by actor type via vertex colors — a
	// real per-shader material mapping is the eventual replacement (see
	// world-object-plan.html "Shader/flora"), this is just for visibility
	// during this pass. Order matters: ASWGPlayer is also an ASWGCreature.
	FVector3f GetPlaceholderColorForActor(const AActor& Actor)
	{
		if (Actor.IsA<ASWGPlayer>())         return FVector3f(0.1f, 0.6f, 1.0f);   // blue
		if (Actor.IsA<ASWGCreature>())       return FVector3f(1.0f, 0.2f, 0.2f);   // red
		if (Actor.IsA<ASWGBuilding>())       return FVector3f(0.9f, 0.7f, 0.1f);   // gold
		if (Actor.IsA<ASWGInstallation>())   return FVector3f(0.7f, 0.2f, 0.9f);   // purple
		if (Actor.IsA<ASWGStaticProp>())     return FVector3f(0.2f, 0.8f, 0.3f);   // green
		if (Actor.IsA<ASWGItem>())           return FVector3f(1.0f, 0.5f, 0.1f);   // orange
		return FVector3f(0.7f, 0.7f, 0.7f); // unknown — neutral gray
	}

	// Shared mesh-population core for BuildDynamicMesh and
	// GetOrBuildGeneratedStaticMesh — appends every submesh's own
	// self-contained vertex buffer into EditMesh, one material ID per
	// submesh so a real per-shader material can be assigned later without
	// restructuring the mesh (see BuildDynamicMesh for the full reasoning).
	void PopulateDynamicMeshFromMeshData(FDynamicMesh3& EditMesh, const FSWGMeshData& MeshData, const FVector3f& PlaceholderColor)
	{
		EditMesh.EnableAttributes();
		EditMesh.Attributes()->EnableMaterialID();
		EditMesh.EnableVertexColors(PlaceholderColor);

		FDynamicMeshNormalOverlay* Normals = EditMesh.Attributes()->PrimaryNormals();
		FDynamicMeshUVOverlay* UVs = EditMesh.Attributes()->PrimaryUV();
		FDynamicMeshMaterialAttribute* MaterialIDs = EditMesh.Attributes()->GetMaterialID();

		for (int32 SubmeshIndex = 0; SubmeshIndex < MeshData.Submeshes.Num(); ++SubmeshIndex)
		{
			const FSWGMeshSubmesh& Submesh = MeshData.Submeshes[SubmeshIndex];

			TArray<int32> VertexIds, NormalIds, UVIds;
			VertexIds.SetNumUninitialized(Submesh.Vertices.Num());
			NormalIds.SetNumUninitialized(Submesh.Vertices.Num());
			UVIds.SetNumUninitialized(Submesh.Vertices.Num());

			for (int32 i = 0; i < Submesh.Vertices.Num(); ++i)
			{
				const FSWGMeshVertex& V = Submesh.Vertices[i];
				VertexIds[i] = EditMesh.AppendVertex((FVector3d)V.Position);
				NormalIds[i] = Normals->AppendElement((FVector3f)V.Normal);
				UVIds[i] = UVs->AppendElement(V.UVs.Num() > 0 ? FVector2f(V.UVs[0]) : FVector2f::Zero());
				EditMesh.SetVertexColor(VertexIds[i], PlaceholderColor);
			}

			for (int32 t = 0; t + 2 < Submesh.Triangles.Num(); t += 3)
			{
				const int32 IA = Submesh.Triangles[t];
				const int32 IB = Submesh.Triangles[t + 1];
				const int32 IC = Submesh.Triangles[t + 2];

				// A malformed/mis-parsed submesh (bad chunk offsets, corrupt
				// data) shouldn't be able to crash the engine — skip out-of-
				// range triangles instead of indexing straight into VertexIds.
				if (!Submesh.Vertices.IsValidIndex(IA) || !Submesh.Vertices.IsValidIndex(IB) || !Submesh.Vertices.IsValidIndex(IC))
				{
					UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: skipping out-of-range triangle (%d,%d,%d) into %d vertices"),
						IA, IB, IC, Submesh.Vertices.Num());
					continue;
				}

				const int32 TriId = EditMesh.AppendTriangle(VertexIds[IA], VertexIds[IB], VertexIds[IC]);
				if (TriId >= 0)
				{
					Normals->SetTriangle(TriId, FIndex3i(NormalIds[IA], NormalIds[IB], NormalIds[IC]));
					UVs->SetTriangle(TriId, FIndex3i(UVIds[IA], UVIds[IB], UVIds[IC]));
					MaterialIDs->SetValue(TriId, SubmeshIndex);
				}
			}
		}
	}

	// The swg.* console commands below are registered once per process (static
	// FAutoConsoleCommand locals in Initialize), but this is a
	// UGameInstanceSubsystem — a fresh instance per PIE session. Capturing
	// `this` bound every command to the FIRST session's (destroyed) instance,
	// making them silently dead ("no TreSubsystem") in any later session. So
	// each command resolves the currently-live instance at invocation time
	// instead.
	USWGMeshGeneratorSubsystem* FindLiveMeshGeneratorSubsystem()
	{
		for (TObjectIterator<USWGMeshGeneratorSubsystem> It; It; ++It)
		{
			if (IsValid(*It) && It->GetGameInstance())
			{
				return *It;
			}
		}
		return nullptr;
	}
}

void USWGMeshGeneratorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	TreSubsystem = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));

	// Temporary diagnostic for the Wookiee UV-mapping investigation ("face
	// appears near the hip") — dumps Position.Z alongside UV for vertices
	// spanning the WHOLE submesh (evenly spaced by index, not just the first
	// few), to check whether UV.V correlates sanely with body height or
	// whether there's a scrambled/discontinuous region partway through.
	static FAutoConsoleCommand DumpMgnUvSpreadCmd(
		TEXT("swg.DumpMgnUvSpread"),
		TEXT("swg.DumpMgnUvSpread <virtualPath> <submeshIndex> — logs Position.Z vs UV for vertices spread evenly across the submesh."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 1)
				{
					UE_LOG(LogTemp, Warning, TEXT("Usage: swg.DumpMgnUvSpread <virtualPath> <submeshIndex>"));
					return;
				}
				USWGMeshGeneratorSubsystem* Self = FindLiveMeshGeneratorSubsystem();
				USWGTreSubsystem* TreSubsystem = Self ? Self->TreSubsystem.Get() : nullptr;
				if (!TreSubsystem)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.DumpMgnUvSpread: no TreSubsystem"));
					return;
				}

				const int32 SubmeshIndex = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 0;

				FSWGIffReader Reader = TreSubsystem->CreateIffReader(Args[0]);
				FSWGMeshData MeshData;
				if (!FSWGMeshReader::ReadSkeletalMeshBindPose(Reader, MeshData))
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.DumpMgnUvSpread: failed to parse '%s'"), *Args[0]);
					return;
				}

				if (!MeshData.Submeshes.IsValidIndex(SubmeshIndex))
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.DumpMgnUvSpread: submesh index %d out of range (%d submesh(es))"), SubmeshIndex, MeshData.Submeshes.Num());
					return;
				}

				const FSWGMeshSubmesh& Submesh = MeshData.Submeshes[SubmeshIndex];
				UE_LOG(LogTemp, Warning, TEXT("swg.DumpMgnUvSpread: submesh[%d] ('%s'), %d vertices"), SubmeshIndex, *Submesh.ShaderName, Submesh.Vertices.Num());

				const int32 SampleCount = 24;
				const int32 Total = Submesh.Vertices.Num();
				for (int32 i = 0; i < SampleCount; ++i)
				{
					const int32 Idx = Total > 1 ? (i * (Total - 1)) / (SampleCount - 1) : 0;
					const FSWGMeshVertex& V = Submesh.Vertices[Idx];
					const FVector2D UV = V.UVs.Num() > 0 ? V.UVs[0] : FVector2D::ZeroVector;
					UE_LOG(LogTemp, Warning, TEXT("  vertex[%d] Z=%.2f UV=(%.4f,%.4f)"), Idx, V.Position.Z, UV.X, UV.Y);
				}
			}));

#if WITH_EDITOR
	// Temporary diagnostic: builds a real USkeletalMesh + USkeleton for the
	// Wookiee (body + head merged) via FSWGSkeletalMeshImporter, as a
	// standalone proof of the .skt/.mgn -> real skinned mesh pipeline before
	// it's wired into the main mesh-generation flow (which still renders
	// every creature bind-pose-only via the procedural generated-mesh path). Remove or
	// fold into that flow once this is validated.
	static FAutoConsoleCommand BuildWookieeSkeletalMeshCmd(
		TEXT("swg.BuildWookieeSkeletalMesh"),
		TEXT("swg.BuildWookieeSkeletalMesh — builds /Game/SWGEmu/Generated/SK_Wookiee_MaterialTableFixed from appearance/skeleton/all_b.skt + the Wookiee body/head .mgn meshes."),
		FConsoleCommandDelegate::CreateLambda([]()
			{
				USWGMeshGeneratorSubsystem* Self = FindLiveMeshGeneratorSubsystem();
				USWGTreSubsystem* TreSubsystem = Self ? Self->TreSubsystem.Get() : nullptr;
				if (!TreSubsystem)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.BuildWookieeSkeletalMesh: no TreSubsystem"));
					return;
				}

				FSWGIffReader SkeletonReaderIff = TreSubsystem->CreateIffReader(TEXT("appearance/skeleton/all_b.skt"));
				FSWGSkeletonData Skeleton;
				if (!FSWGSkeletonReader::ReadSkeleton(SkeletonReaderIff, Skeleton))
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.BuildWookieeSkeletalMesh: failed to parse skeleton"));
					return;
				}

				FSWGIffReader BodyReaderIff = TreSubsystem->CreateIffReader(TEXT("appearance/mesh/wke_m_body_l0.mgn"));
				FSWGMeshData BodyMesh;
				if (!FSWGMeshReader::ReadSkeletalMeshBindPose(BodyReaderIff, BodyMesh))
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.BuildWookieeSkeletalMesh: failed to parse body mesh"));
					return;
				}

				FSWGIffReader HeadReaderIff = TreSubsystem->CreateIffReader(TEXT("appearance/mesh/wke_m_head_l0.mgn"));
				FSWGMeshData HeadMesh;
				if (!FSWGMeshReader::ReadSkeletalMeshBindPose(HeadReaderIff, HeadMesh))
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.BuildWookieeSkeletalMesh: failed to parse head mesh"));
					return;
				}

				FSWGSkeletonData FaceSkeleton;
				if (FSWGSkeletonReader::ReadSkeleton(TreSubsystem->CreateIffReader(TEXT("appearance/skeleton/wke_m_face.skt")), FaceSkeleton))
				{
					const int32 HeadJointIndex = Skeleton.Joints.IndexOfByPredicate([](const FSWGSkeletonJoint& Joint) { return Joint.Name.Equals(TEXT("head"), ESearchCase::IgnoreCase); });
					if (HeadJointIndex != INDEX_NONE)
					{
						const int32 FaceBaseIndex = Skeleton.Joints.Num();
						for (FSWGSkeletonJoint Joint : FaceSkeleton.Joints)
						{
							Joint.ParentIndex = Joint.ParentIndex == INDEX_NONE ? HeadJointIndex : FaceBaseIndex + Joint.ParentIndex;
							Skeleton.Joints.Add(MoveTemp(Joint));
						}
					}
				}

				const TArray<const FSWGMeshData*> MeshParts = { &BodyMesh, &HeadMesh };
				USkeletalMesh* Result = FSWGSkeletalMeshImporter::BuildSkeletalMesh(
					Skeleton,
					MeshParts,
					TEXT("/Game/SWGEmu/Generated/SK_Wookiee_MaterialTableFixed"),
					Self->GetSlotHardpoints());
				if (!Result)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.BuildWookieeSkeletalMesh: build failed — see preceding warnings"));
					return;
				}

				UE_LOG(LogTemp, Warning, TEXT("swg.BuildWookieeSkeletalMesh: success — /Game/SWGEmu/Generated/SK_Wookiee_MaterialTableFixed"));
			}));

	// Live material-slot isolation for the generated Wookiee. The body and
	// head source meshes each contribute their own sections, so this pinpoints
	// a stray texture patch without rebuilding the skeletal asset. For example:
	//   swg.WookieeShowMaterial 0 0
	// hides slot 0; pass 1 to show it again. Slot -1 restores every slot.
	static FAutoConsoleCommand WookieeShowMaterialCmd(
		TEXT("swg.WookieeShowMaterial"),
		TEXT("swg.WookieeShowMaterial <slot|-1> <0|1> — hides/shows a generated Wookiee material slot; -1 restores all slots."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 2)
				{
					UE_LOG(LogTemp, Warning, TEXT("Usage: swg.WookieeShowMaterial <slot|-1> <0|1>"));
					return;
				}

				const int32 SlotIndex = FCString::Atoi(*Args[0]);
				const bool bShow = FCString::Atoi(*Args[1]) != 0;
				int32 AffectedComponents = 0;
				for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
				{
					USkeletalMeshComponent* Component = *It;
					if (!IsValid(Component) || !Component->GetWorld() || !Component->GetWorld()->IsGameWorld())
					{
						continue;
					}
					const USkeletalMesh* Mesh = Cast<USkeletalMesh>(Component->GetSkinnedAsset());
					if (!Mesh || !Mesh->GetPathName().Contains(TEXT("SK_Wookiee_MaterialTableFixed")))
					{
						continue;
					}

					if (SlotIndex < 0)
					{
						Component->ShowAllMaterialSections(0);
					}
					else if (Mesh->GetMaterials().IsValidIndex(SlotIndex))
					{
						// INDEX_NONE deliberately bypasses any LOD section remap: this
						// command addresses the generated asset's material slot directly.
						Component->ShowMaterialSection(SlotIndex, INDEX_NONE, bShow, 0);
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("swg.WookieeShowMaterial: slot %d is invalid (mesh has %d slots)"), SlotIndex, Mesh->GetMaterials().Num());
						continue;
					}
					++AffectedComponents;
				}

				UE_LOG(LogTemp, Warning, TEXT("swg.WookieeShowMaterial: %s slot %d on %d Wookiee component(s)"), bShow ? TEXT("showed") : TEXT("hid"), SlotIndex, AffectedComponents);
			}));

	// Temporary diagnostic for the Wookiee flat-white/palette-tint
	// investigation — logs exactly what ResolveShaderTintPalettePath and
	// LoadPaletteAverageTint resolve for a given shader, without needing a
	// full PIE session. Remove once the tint pipeline is confirmed working.
	static FAutoConsoleCommand DumpShaderTintCmd(
		TEXT("swg.DumpShaderTint"),
		TEXT("swg.DumpShaderTint <shaderVirtualPath> — resolves the shader's TFAC palette path and its averaged tint color."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 1)
				{
					UE_LOG(LogTemp, Warning, TEXT("Usage: swg.DumpShaderTint <shaderVirtualPath>"));
					return;
				}
				USWGMeshGeneratorSubsystem* Self = FindLiveMeshGeneratorSubsystem();
				if (!Self)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.DumpShaderTint: no live USWGMeshGeneratorSubsystem"));
					return;
				}

				const FString PalettePath = Self->ResolveShaderTintPalettePath(Args[0]);
				UE_LOG(LogTemp, Warning, TEXT("swg.DumpShaderTint: shader='%s' palettePath='%s'"), *Args[0], *PalettePath);

				if (!PalettePath.IsEmpty())
				{
					const FLinearColor Tint = Self->LoadPaletteAverageTint(PalettePath);
					UE_LOG(LogTemp, Warning, TEXT("swg.DumpShaderTint: tint=(%.3f,%.3f,%.3f)"), Tint.R, Tint.G, Tint.B);
				}
			}));

	// Builds real UAnimSequence assets for the Wookiee's idle and walk
	// cycles from parsed .ans data, bound to the SK_Wookiee_Skeleton built by
	// swg.BuildWookieeSkeletalMesh (run that first). Rotation-only for now —
	// see FSWGAnimationImporter's header comment.
	static FAutoConsoleCommand BuildWookieeAnimationsCmd(
		TEXT("swg.BuildWookieeAnimations"),
		TEXT("swg.BuildWookieeAnimations — builds Anim_Wookiee_Idle and Anim_Wookiee_Walk from all_b_idl_standing_idle1.ans and all_b_loc_walk_male.ans, bound to /Game/SWGEmu/Generated/SK_Wookiee_Skeleton."),
		FConsoleCommandDelegate::CreateLambda([]()
			{
				USWGMeshGeneratorSubsystem* Self = FindLiveMeshGeneratorSubsystem();
				USWGTreSubsystem* TreSubsystem = Self ? Self->TreSubsystem.Get() : nullptr;
				if (!TreSubsystem)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.BuildWookieeAnimations: no TreSubsystem"));
					return;
				}

				USkeleton* WookieeSkeleton = LoadObject<USkeleton>(nullptr, TEXT("/Game/SWGEmu/Generated/SK_Wookiee_Skeleton.SK_Wookiee_Skeleton"));
				if (!WookieeSkeleton)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.BuildWookieeAnimations: /Game/SWGEmu/Generated/SK_Wookiee_Skeleton not found — run swg.BuildWookieeSkeletalMesh first"));
					return;
				}

				FSWGIffReader SkeletonReaderIff = TreSubsystem->CreateIffReader(TEXT("appearance/skeleton/all_b.skt"));
				FSWGSkeletonData Skeleton;
				if (!FSWGSkeletonReader::ReadSkeleton(SkeletonReaderIff, Skeleton))
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.BuildWookieeAnimations: failed to parse skeleton"));
					return;
				}

				auto BuildOne = [&Skeleton, WookieeSkeleton, TreSubsystem](const FString& AnsPath, const FString& AssetPath)
				{
					FSWGIffReader AnimReaderIff = TreSubsystem->CreateIffReader(AnsPath);
					FSWGAnimationData Animation;
					if (!FSWGAnimationReader::ReadAnimation(AnimReaderIff, Animation))
					{
						UE_LOG(LogTemp, Warning, TEXT("swg.BuildWookieeAnimations: failed to parse '%s'"), *AnsPath);
						return;
					}
					if (!FSWGAnimationImporter::BuildAnimSequence(Animation, Skeleton, WookieeSkeleton, AssetPath))
					{
						UE_LOG(LogTemp, Warning, TEXT("swg.BuildWookieeAnimations: failed to build '%s'"), *AssetPath);
					}
				};

				BuildOne(TEXT("appearance/animation/all_b_idl_standing_idle1.ans"), TEXT("/Game/SWGEmu/Generated/Anim_Wookiee_Idle"));
				BuildOne(TEXT("appearance/animation/all_b_loc_walk_male.ans"), TEXT("/Game/SWGEmu/Generated/Anim_Wookiee_Walk"));

				UE_LOG(LogTemp, Warning, TEXT("swg.BuildWookieeAnimations: done"));
			}));

	// Diagnostic — see GSWGDebugAnsBoneFilter's comment (SWGAnimationReader.cpp)
	// and WOOKIEE_ANIMATION_POSE_BUG.md. Re-parses the given .ans and logs raw
	// QCHN scale bytes + every decoded keyframe's swing angle/axis for bones
	// whose name contains any of the comma-separated filter terms, e.g.
	// "swg.DumpAnsAnimation appearance/animation/all_b_loc_walk_male.ans root,thigh,elbow".
	static FAutoConsoleCommand DumpAnsAnimationCmd(
		TEXT("swg.DumpAnsAnimation"),
		TEXT("swg.DumpAnsAnimation <virtualPath> <comma,separated,boneNameFilters> — logs raw CKAT scale bytes and per-keyframe swing angle/axis for matching bones."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 2)
				{
					UE_LOG(LogTemp, Warning, TEXT("Usage: swg.DumpAnsAnimation <virtualPath> <comma,separated,boneNameFilters>"));
					return;
				}
				USWGMeshGeneratorSubsystem* Self = FindLiveMeshGeneratorSubsystem();
				USWGTreSubsystem* TreSubsystem = Self ? Self->TreSubsystem.Get() : nullptr;
				if (!TreSubsystem)
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.DumpAnsAnimation: no TreSubsystem"));
					return;
				}

				GSWGDebugAnsBoneFilter = Args[1];

				FSWGIffReader Reader = TreSubsystem->CreateIffReader(Args[0]);
				FSWGAnimationData Animation;
				if (!FSWGAnimationReader::ReadAnimation(Reader, Animation))
				{
					UE_LOG(LogTemp, Warning, TEXT("swg.DumpAnsAnimation: failed to parse '%s'"), *Args[0]);
				}

				GSWGDebugAnsBoneFilter.Empty();
			}));

#endif
}

void USWGMeshGeneratorSubsystem::Deinitialize()
{
	PendingRequests.Reset();
}

void USWGMeshGeneratorSubsystem::Tick(float DeltaTime)
{
	// One request per tick for now — spreads mesh-build work across frames
	// instead of hitching on a backlog, matching the reasoning (if not yet the
	// background-thread mechanics) behind USWGTerrainSubsystem's async bake.
	if (PendingRequests.Num() > 0)
	{
		ProcessNextRequest();
	}

	// Feeds each actor's live horizontal speed into its blend space every
	// tick — the blend space itself owns sample selection, blending, and
	// (via AxisToScaleAnimation, set in GetOrBuildLocomotionBlendSpace)
	// playback-rate scaling; see TryApplyGeneratedAnimatedMesh.
	for (int32 i = PlayingAnimations.Num() - 1; i >= 0; --i)
	{
		FSWGPlayingAnimation& Playing = PlayingAnimations[i];
		USkeletalMeshComponent* MeshComponent = Playing.MeshComponent.Get();
		UAnimSingleNodeInstance* AnimInstance = Playing.AnimInstance.Get();
		if (!MeshComponent || !AnimInstance)
		{
			PlayingAnimations.RemoveAtSwap(i);
			continue;
		}

		const ACharacter* Character = Cast<ACharacter>(MeshComponent->GetOwner());
		float HorizontalSpeed = Character ? Character->GetVelocity().Size2D() : 0.0f;
		if (const USWGMovementComponent* Movement = Character ? Cast<USWGMovementComponent>(Character->GetCharacterMovement()) : nullptr)
		{
			// LastNetworkUpdateTime > 0 means this is a network-driven actor
			// (see its own comment) — the server just stops sending updates
			// when it stops moving rather than sending an explicit zero-speed
			// one, so a stale update means "stopped," not "still going at the
			// last derived speed." Actors that have never taken a network
			// update (the locally-controlled player) skip this entirely and
			// keep using their real CharacterMovementComponent velocity.
			if (Movement->LastNetworkUpdateTime > 0.0f && MeshComponent->GetWorld())
			{
				const float TimeSinceUpdate = MeshComponent->GetWorld()->GetTimeSeconds() - Movement->LastNetworkUpdateTime;
				if (TimeSinceUpdate > 0.5f)
				{
					HorizontalSpeed = 0.0f;
				}
			}
		}
		AnimInstance->SetBlendSpacePosition(FVector(HorizontalSpeed, 0.0f, 0.0f));
	}
}

TStatId USWGMeshGeneratorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGMeshGeneratorSubsystem, STATGROUP_Tickables);
}

bool USWGMeshGeneratorSubsystem::IsTickable() const
{
	return true;
}

void USWGMeshGeneratorSubsystem::RequestMesh(AActor* Actor, const FString& MeshVirtualPath, bool bSkeletal)
{
	if (!Actor)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGMeshGeneratorSubsystem: RequestMesh called with a null actor"));
		return;
	}

	FSWGPendingMeshRequest Request;
	Request.Actor = Actor;
	Request.MeshVirtualPaths = { MeshVirtualPath };
	Request.bSkeletal = bSkeletal;
	PendingRequests.Add(MoveTemp(Request));
}

void USWGMeshGeneratorSubsystem::RequestMesh(AActor* Actor, const uint32 TemplateCrc)
{
	if (!Actor)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGMeshGeneratorSubsystem: RequestMesh called with a null actor"));
		return;
	}

	FSWGPendingMeshRequest Request;
	Request.Actor = Actor;
	Request.TemplateCrc = TemplateCrc;
	PendingRequests.Add(MoveTemp(Request));
}

void USWGMeshGeneratorSubsystem::RequestMeshForTemplatePath(AActor* Actor, const FString& TemplatePath)
{
	if (!Actor)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGMeshGeneratorSubsystem: RequestMeshForTemplatePath called with a null actor"));
		return;
	}

	FSWGPendingMeshRequest Request;
	Request.Actor = Actor;
	Request.TemplatePath = TemplatePath;
	// Only world-snapshot objects (buildings, static props, terrain
	// decoration) go through this entry point — see FSWGPendingMeshRequest::bStatic.
	Request.bStatic = true;
	PendingRequests.Add(MoveTemp(Request));
}

void USWGMeshGeneratorSubsystem::ProcessNextRequest()
{
	// TODO: pop PendingRequests[0], call ResolveMeshPath (if the request only
	// carries a CRC, not yet supported) then ParseMesh then BuildDynamicMesh,
	// broadcasting OnMeshReady/OnMeshError. See USWGTerrainSubsystem::LoadTerrain
	// for the equivalent parse->bake->spawn sequencing this should mirror once
	// mesh parsing moves to a background thread.

	if (PendingRequests.Num() == 0)
	{
		return;
	}

	uint64 StartTime = FPlatformTime::Cycles64();
	uint64 Limit = 3 * (FPlatformTime::SecondsToCycles64(1) / 1000);

	while (FPlatformTime::Cycles64() - StartTime < Limit && PendingRequests.Num() > 0)
	{

		FSWGPendingMeshRequest Request = PendingRequests.Pop();
		// Diagnostic: pins down where a request silently vanishes without hitting
		// any existing error/warning log.
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: ProcessNextRequest starting for actor %s (crc %08X, %d path(s) already resolved)"),
			Request.Actor.IsValid() ? *Request.Actor->GetName() : TEXT("<gone>"), Request.TemplateCrc, Request.MeshVirtualPaths.Num());
		Async(EAsyncExecution::Thread, [this, Request = MoveTemp(Request)]() mutable
			{

				if (Request.TemplateCrc != 0 && Request.MeshVirtualPaths.IsEmpty())
				{
					if (!ResolveMeshPath(Request.TemplateCrc, Request.MeshVirtualPaths, Request.AnimationLatPaths, Request.bSkeletal, Request.AppearancePath))
					{
						UE_LOG(LogTemp, Error, TEXT("USWGMeshGeneratorSubsystem: failed to resolve mesh path for template CRC %08X"), Request.TemplateCrc);
						return;
					}
				}
				else if (!Request.TemplatePath.IsEmpty() && Request.MeshVirtualPaths.IsEmpty())
				{
					if (!ResolveMeshPathForTemplate(Request.TemplatePath, Request.MeshVirtualPaths, Request.AnimationLatPaths, Request.bSkeletal, Request.AppearancePath))
					{
						UE_LOG(LogTemp, Error, TEXT("USWGMeshGeneratorSubsystem: failed to resolve mesh path for template %s"), *Request.TemplatePath);
						return;
					}
				}

				if (Request.MeshVirtualPaths.IsEmpty())
				{
					UE_LOG(LogTemp, Error, TEXT("USWGMeshGeneratorSubsystem: no mesh path to parse for actor %s (template CRC %08X)"), Request.Actor.IsValid() ? *Request.Actor->GetName() : TEXT("<gone>"), Request.TemplateCrc);
					return;
				}

				UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: resolved %d mesh path(s) for actor %s (crc %08X): %s"),
					Request.MeshVirtualPaths.Num(), Request.Actor.IsValid() ? *Request.Actor->GetName() : TEXT("<gone>"), Request.TemplateCrc, *FString::Join(Request.MeshVirtualPaths, TEXT(", ")));

				FSWGMeshData MeshData;
				const bool bParsed = ParseMesh(Request, MeshData);
				UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: ParseMesh returned %s for actor %s (crc %08X), %d submesh(es)"),
					bParsed ? TEXT("true") : TEXT("false"), Request.Actor.IsValid() ? *Request.Actor->GetName() : TEXT("<gone>"), Request.TemplateCrc, MeshData.Submeshes.Num());
				if (!bParsed)
				{
					return;
				}

				// Every generated mesh (world-snapshot static objects and
				// creature/player/item procedural ones alike) is a real
				// UStaticMesh cached under /Game/SWGEmu/Generated/SM_<hash> —
				// see GetOrBuildGeneratedStaticMesh — instead of the old
				// ephemeral Saved/MeshCache/*.dmesh raw-serialization cache.
				// Static objects key on TemplatePath alone (one asset shared
				// by every placed instance of that template — its geometry
				// never varies per-instance). Everything else keys on the
				// resolved mesh file(s) + actor class, matching the old
				// .dmesh key: PlaceholderColor (baked into the mesh's vertex
				// colors) is a pure function of actor class, so two classes
				// sharing the same underlying mesh file(s) must not share
				// one cache entry, or whichever built it first "poisons" the
				// tint for the other.
				const FString ActorClassName = Request.Actor.IsValid() ? Request.Actor->GetClass()->GetName() : TEXT("Unknown");
				const uint32 CacheHash = Request.bStatic
					? GetTypeHash(Request.TemplatePath)
					: (GetTypeHash(Request.MeshVirtualPaths) ^ GetTypeHash(ActorClassName));
				const FString DebugName = Request.bStatic
					? Request.TemplatePath
					: FString::Printf(TEXT("%s (%s)"), *FString::Join(Request.MeshVirtualPaths, TEXT(", ")), *ActorClassName);
				const float YawCorrectionDegrees = GetStaticMeshYawCorrection(Request.MeshVirtualPaths);
				const EComponentMobility::Type Mobility = Request.bStatic ? EComponentMobility::Static : EComponentMobility::Movable;

				// Back on the game thread to build/attach the mesh component.
				// The actor can be destroyed/GC'd during this background parse
				// (streaming out, zone change, etc.) — Request.Actor is a
				// TWeakObjectPtr specifically for that reason.
				AsyncTask(ENamedThreads::GameThread, [this, Request, MeshData, CacheHash, DebugName, YawCorrectionDegrees, Mobility]()
					{
						if (!Request.Actor.IsValid())
						{
							UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: actor gone before mesh build completed for %s — skipping"), *DebugName);
							return;
						}

						// Real per-character skin/hair/eye (etc.) palette
						// colors and face/body morph weights, if the actor's
						// TANO baseline carried any customization data and we
						// know its appearance file (see
						// ResolveCustomizationPaletteTints/
						// ResolveCustomizationMorphWeights) — falls through to
						// GetOrBuildObjectMaterial's usual shader-average tint
						// (and no morph deformation at all) otherwise.
						TMap<FString, FLinearColor> PaletteTints;
						TMap<FString, float> MorphWeights;
						TMap<FString, int32> TextureIndices;
						if (USWGTangibleComponent* Tangible = Request.Actor->FindComponentByClass<USWGTangibleComponent>())
						{
							if (Tangible->DecodedCustomization.Values.Num() > 0 && !Request.AppearancePath.IsEmpty())
							{
								PaletteTints = ResolveCustomizationPaletteTints(Tangible->DecodedCustomization, Request.AppearancePath);
								MorphWeights = ResolveCustomizationMorphWeights(Tangible->DecodedCustomization, Request.AppearancePath);
								TextureIndices = ResolveCustomizationTextureIndices(Tangible->DecodedCustomization, Request.AppearancePath);
							}
						}

						const TMap<FString, FLinearColor>* PaletteTintsPtr = PaletteTints.Num() > 0 ? &PaletteTints : nullptr;
						const TMap<FString, float>* MorphWeightsPtr = MorphWeights.Num() > 0 ? &MorphWeights : nullptr;
						const TMap<FString, int32>* TextureIndicesPtr = TextureIndices.Num() > 0 ? &TextureIndices : nullptr;
						UMeshComponent* MeshComponent = BuildGeneratedMeshComponent(*Request.Actor, MeshData, CacheHash, DebugName, YawCorrectionDegrees, Mobility, PaletteTintsPtr, TextureIndicesPtr);
						TryApplyGeneratedAnimatedMesh(*Request.Actor, Request.MeshVirtualPaths, Request.AnimationLatPaths, MeshComponent, PaletteTintsPtr, MorphWeightsPtr, TextureIndicesPtr);
					});
			});
	}
}

namespace
{
	// A .sat's MSGN chunk is not one string — for a humanoid appearance it's
	// several null-terminated body-part .lmg references packed back to back
	// (arms/body/hands/head, confirmed via hex dump of appearance/hum_m.sat:
	// DataSize=125 held four "appearance/mesh/hum_m_*.lmg\0" strings, not one
	// garbled 124-char blob). ReadFullChunkString's "whole chunk is one
	// string" assumption (correct for NAME/appearanceFilename-style chunks)
	// doesn't hold here.
	TArray<FString> SplitNullTerminatedStrings(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk)
	{
		TArray<FString> Result;
		const uint8* Data = Reader.GetChunkData(Chunk);
		int32 Start = 0;
		for (int32 i = 0; i < Chunk.DataSize; ++i)
		{
			if (Data[i] == 0)
			{
				if (i > Start)
				{
					Result.Add(FString::ConstructFromPtrSize((const ANSICHAR*)(Data + Start), i - Start));
				}
				Start = i + 1;
			}
		}
		return Result;
	}
}

bool USWGMeshGeneratorSubsystem::ResolveMeshPath(uint32 TemplateCrc, TArray<FString>& OutMeshVirtualPaths, TMap<FString, FString>& OutAnimationLatPaths, bool& bOutSkeletal, FString& OutAppearancePath)
{
	const FString TemplatePath = TreSubsystem->ResolveTemplatePath(TemplateCrc);
	if (TemplatePath.IsEmpty())
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: no template path for CRC %08X"), TemplateCrc);
		return false;
	}

	return ResolveMeshPathForTemplate(TemplatePath, OutMeshVirtualPaths, OutAnimationLatPaths, bOutSkeletal, OutAppearancePath);
}

bool USWGMeshGeneratorSubsystem::ResolveMeshPathForTemplate(const FString& TemplatePath, TArray<FString>& OutMeshVirtualPaths, TMap<FString, FString>& OutAnimationLatPaths, bool& bOutSkeletal, FString& OutAppearancePath)
{
	// Resolution chain:
	//   template .iff (SCOT/STOT > FORM SHOT > versioned data form > XXXX
	//     "appearanceFilename" key) -> appearance/*.sat or appearance/*.apt
	//   .sat (FORM SMAT > FORM 0003 > MSGN, one full path)      -> .lmg
	//   .lmg (FORM MLOD > FORM 0000 > multiple NAME, full paths) -> .mgn (skeletal)
	//   .apt (FORM "APT " > FORM 0000 > NAME, one full path)     -> .lod
	//   .lod (FORM DTLA > FORM 0007 > FORM DATA > multiple CHLD,
	//         [4-byte index][path relative to "appearance/"])    -> .msh (static)
	// Note: some templates (e.g. abstract containers like creature inventory)
	// have an empty appearanceFilename — correctly fails here, not a bug.

	FSWGIffReader TemplateReader = TreSubsystem->CreateIffReader(TemplatePath);
	if (!TemplateReader.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: template %s failed to open as an IFF reader"), *TemplatePath);
		return false;
	}

	FSWGIffChunk ShotForm, ShotDataForm;
	if (!TemplateReader.FindForm(SWG_IFF_TAG('S','H','O','T'), ShotForm) || !FindVersionedDataForm(TemplateReader, ShotForm, ShotDataForm))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: template %s has no SHOT data form"), *TemplatePath);
		return false;
	}

	// Walk the DERV inheritance chain (see FindDervParentPath) looking for
	// whichever layer sets appearanceFilename OR portalLayoutFilename — most
	// concrete templates (buildings especially) leave both empty locally and
	// inherit them from a shared base, and which one is set (and at which
	// layer) isn't consistent between simple props and portal buildings.
	FString AppearancePath;
	FString PobPath;
	bool bFoundAppearance = false;
	bool bFoundPobPath = false;
	{
		TOptional<FSWGIffReader> CurrentReader;
		const FSWGIffReader* ReaderPtr = &TemplateReader;
		FSWGIffChunk CurrentShotForm = ShotForm;
		FSWGIffChunk CurrentDataForm = ShotDataForm;
		FString CurrentPath = TemplatePath;

		for (int32 Depth = 0; Depth < 8; ++Depth)
		{
			if (!bFoundAppearance && FindAppearanceFilename(*ReaderPtr, CurrentDataForm, AppearancePath))
			{
				bFoundAppearance = true;
			}
			if (!bFoundPobPath && FindXxxxStringValue(*ReaderPtr, CurrentDataForm, TEXT("portalLayoutFilename"), PobPath))
			{
				bFoundPobPath = true;
			}
			if (bFoundAppearance || bFoundPobPath)
			{
				break;
			}

			FString ParentPath;
			if (!FindDervParentPath(*ReaderPtr, CurrentShotForm, ParentPath))
			{
				UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: %s (depth %d) has no DERV form, chain ends here"), *CurrentPath, Depth);
				break;
			}

			UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: %s (depth %d) DERVs to %s"), *CurrentPath, Depth, *ParentPath);

			FSWGIffReader ParentReader = TreSubsystem->CreateIffReader(ParentPath);
			FSWGIffChunk ParentShotForm, ParentDataForm;
			if (!ParentReader.IsValid()
				|| !ParentReader.FindForm(SWG_IFF_TAG('S','H','O','T'), ParentShotForm)
				|| !FindVersionedDataForm(ParentReader, ParentShotForm, ParentDataForm))
			{
				UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: DERV parent %s (from %s) has no usable SHOT data form"), *ParentPath, *CurrentPath);
				break;
			}

			CurrentReader.Emplace(MoveTemp(ParentReader));
			ReaderPtr = &CurrentReader.GetValue();
			CurrentShotForm = ParentShotForm;
			CurrentDataForm = ParentDataForm;
			CurrentPath = ParentPath;
		}
	}

	if (!bFoundAppearance && bFoundPobPath)
	{
		// Buildings with interiors: no appearanceFilename anywhere, but a
		// portalLayoutFilename (.pob) whose "outside" cell carries the exterior
		// shell mesh (see ReadPobExteriorMeshPath).
		FSWGIffReader PobReader = TreSubsystem->CreateIffReader(PobPath);
		if (PobReader.IsValid() && ReadPobExteriorMeshPath(PobReader, AppearancePath))
		{
			bFoundAppearance = true;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: template %s has portalLayoutFilename %s but no exterior mesh in its outside cell"), *TemplatePath, *PobPath);
		}
	}

	if (!bFoundAppearance)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: template %s has no appearanceFilename anywhere in its DERV chain (likely abstract/non-tangible, or a portal-layout-only building)"), *TemplatePath);
		DumpAllXxxxKeys(TemplateReader, ShotDataForm, TemplatePath);
		return false;
	}

	// Set as soon as it's confirmed non-empty (not just on the final success
	// return) since it's also the ACST lookup key — worth exposing even if a
	// later step in this same resolution fails (e.g. no usable mesh
	// candidates), so a caller could still resolve customization separately.
	OutAppearancePath = AppearancePath;

	const bool bIsSat = AppearancePath.EndsWith(TEXT(".sat"));
	const bool bIsApt = AppearancePath.EndsWith(TEXT(".apt"));
	// Buildings' portalLayoutFilename-derived exterior path (see
	// ReadPobExteriorMeshPath) is sometimes already a bare .lod reference
	// rather than going through an intermediate .apt — same .lod format
	// either way, just skip straight to the mesh-group loop below with this
	// one path instead of opening/parsing an .apt first.
	const bool bIsLod = AppearancePath.EndsWith(TEXT(".lod"));
	if (!bIsSat && !bIsApt && !bIsLod)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: unrecognized appearanceFilename extension: %s"), *AppearancePath);
		return false;
	}

	TArray<FString> MeshGroupPaths;
	if (bIsLod)
	{
		MeshGroupPaths = { AppearancePath };
		bOutSkeletal = false;
	}
	else
	{

	FSWGIffReader AppearanceReader = TreSubsystem->CreateIffReader(AppearancePath);
	if (!AppearanceReader.IsValid())
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: failed to open appearance file %s (from template %s)"), *AppearancePath, *TemplatePath);
		return false;
	}

	if (bIsSat)
	{
		FSWGIffChunk SmatForm, F0003, MsgnChunk, LatxChunk;
		if (!AppearanceReader.FindForm(SWG_IFF_TAG('S','M','A','T'), SmatForm)
			|| !AppearanceReader.FindChildForm(SmatForm, SWG_IFF_TAG('0','0','0','3'), F0003)
			|| !AppearanceReader.FindChildChunk(F0003, SWG_IFF_TAG('M','S','G','N'), MsgnChunk))
		{
			UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: %s missing SMAT/0003/MSGN structure"), *AppearancePath);
			return false;
		}

		// One or more null-terminated body-part .lmg references packed into
		// this single chunk (see SplitNullTerminatedStrings comment).
		MeshGroupPaths = SplitNullTerminatedStrings(AppearanceReader, MsgnChunk);
		if (AppearanceReader.FindChildChunk(F0003, SWG_IFF_TAG('L','A','T','X'), LatxChunk))
		{
			ReadSatLatPaths(AppearanceReader, LatxChunk, OutAnimationLatPaths);
		}
		bOutSkeletal = true;
	}
	else
	{
		FSWGIffChunk AptForm, Form0000, NameChunk;
		if (!AppearanceReader.FindForm(SWG_IFF_TAG('A','P','T',' '), AptForm)
			|| !AppearanceReader.FindChildForm(AptForm, SWG_IFF_TAG('0','0','0','0'), Form0000)
			|| !AppearanceReader.FindChildChunk(Form0000, SWGIffTags::Name, NameChunk))
		{
			UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: %s missing APT/0000/NAME structure"), *AppearancePath);
			return false;
		}
		MeshGroupPaths = { ReadFullChunkString(AppearanceReader, NameChunk) };
		bOutSkeletal = false;
	}

	} // !bIsLod

	if (MeshGroupPaths.IsEmpty())
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: %s resolved no mesh group paths"), *AppearancePath);
		return false;
	}

	for (const FString& MeshGroupPath : MeshGroupPaths)
	{
		FSWGIffReader GroupReader = TreSubsystem->CreateIffReader(MeshGroupPath);
		if (!GroupReader.IsValid())
		{
			UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: failed to open mesh group file %s (from appearance %s)"), *MeshGroupPath, *AppearancePath);
			continue;
		}

		TArray<FString> Candidates;
		if (bOutSkeletal)
		{
			// .lmg: FORM MLOD > FORM 0000 > multiple NAME (full paths already).
			FSWGIffChunk MlodForm, Form0000;
			if (!GroupReader.FindForm(SWG_IFF_TAG('M','L','O','D'), MlodForm) || !GroupReader.FindChildForm(MlodForm, SWG_IFF_TAG('0','0','0','0'), Form0000))
			{
				UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: %s missing MLOD/0000 structure"), *MeshGroupPath);
				continue;
			}
			for (const FSWGIffChunk& Child : GroupReader.ReadChildren(Form0000))
			{
				if (Child.Tag == SWGIffTags::Name)
				{
					Candidates.Add(ReadFullChunkString(GroupReader, Child));
				}
			}
		}
		else
		{
			// .lod: FORM DTLA > FORM 0007 > FORM DATA > multiple CHLD
			// ([4-byte index][path relative to "appearance/", not full]).
			FSWGIffChunk DtlaForm, Form0007, DataForm;
			if (!GroupReader.FindForm(SWG_IFF_TAG('D','T','L','A'), DtlaForm)
				|| !GroupReader.FindChildForm(DtlaForm, SWG_IFF_TAG('0','0','0','7'), Form0007)
				|| !GroupReader.FindChildForm(Form0007, SWGIffTags::Data, DataForm))
			{
				UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: %s missing DTLA/0007/DATA structure"), *MeshGroupPath);
				continue;
			}
			for (const FSWGIffChunk& Child : GroupReader.ReadChildren(DataForm))
			{
				if (Child.Tag == SWG_IFF_TAG('C','H','L','D') && Child.DataSize > 4)
				{
					const uint8* Data = GroupReader.GetChunkData(Child);
					const FString RelativePath = FString::ConstructFromPtrSize((const ANSICHAR*)(Data + 4), Child.DataSize - 4 - 1);
					Candidates.Add(TEXT("appearance/") + RelativePath);
				}
			}
		}

		const FString FinalPath = PickHighestDetailLod(Candidates);
		if (!FinalPath.IsEmpty())
		{
			OutMeshVirtualPaths.Add(FinalPath);
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: %s produced no mesh candidates"), *MeshGroupPath);
		}
	}

	return !OutMeshVirtualPaths.IsEmpty();
}

bool USWGMeshGeneratorSubsystem::ParseMesh(const FSWGPendingMeshRequest& Request, FSWGMeshData& OutMeshData)
{
	// Usually one path, but a humanoid skeletal appearance resolves to several
	// body-part .mgn paths (arms/body/hands/head — see ResolveMeshPath) that
	// all need parsing and merging into one combined mesh.
	for (const FString& MeshVirtualPath : Request.MeshVirtualPaths)
	{
		FSWGIffReader IffReader = TreSubsystem->CreateIffReader(MeshVirtualPath);
		if (!IffReader.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("USWGMeshGeneratorSubsystem: Failed to create IFF reader for %s"), *MeshVirtualPath);
			continue;
		}

		FSWGMeshData PartData;
		if (Request.bSkeletal)
		{
			if (!FSWGMeshReader::ReadSkeletalMeshBindPose(IffReader, PartData))
			{
				UE_LOG(LogTemp, Error, TEXT("USWGMeshGeneratorSubsystem: Failed to read skeletal mesh bind pose for %s"), *MeshVirtualPath);
				continue;
			}

			// Diagnostic: RAW decoded bind-pose bounds per body part, to tell
			// whether POSN data itself is near-flat (missing bone-transform
			// step) or something later in the pipeline collapses it.
			{
				FBox PartBounds(ForceInit);
				int32 PartVertexCount = 0;
				for (const FSWGMeshSubmesh& Submesh : PartData.Submeshes)
				{
					PartVertexCount += Submesh.Vertices.Num();
					for (const FSWGMeshVertex& V : Submesh.Vertices)
					{
						PartBounds += V.Position;
					}
				}
				const FVector Extent = PartBounds.IsValid ? PartBounds.GetExtent() : FVector::ZeroVector;
				UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: %s parsed %d submeshes, %d vertices, bounds extent=(%.2f,%.2f,%.2f) min=%s max=%s"),
					*MeshVirtualPath, PartData.Submeshes.Num(), PartVertexCount, Extent.X, Extent.Y, Extent.Z,
					*PartBounds.Min.ToString(), *PartBounds.Max.ToString());
			}
		}
		else
		{
			if (!FSWGMeshReader::ReadStaticMesh(IffReader, PartData))
			{
				UE_LOG(LogTemp, Error, TEXT("USWGMeshGeneratorSubsystem: Failed to read static mesh for %s"), *MeshVirtualPath);
				continue;
			}
		}

		OutMeshData.Submeshes.Append(MoveTemp(PartData.Submeshes));
	}

	return OutMeshData.Submeshes.Num() > 0;
}

namespace
{
	// Simple item/weapon shaders are a bare top-level FORM SSHT, but
	// character/creature skin shaders wrap it as FORM CSHD > FORM 0001 >
	// FORM SSHT, and animated shaders (console screens, signs) use a
	// completely different top-level FORM SWTS. Recurses to find whichever
	// TargetFormType form is present, regardless of what wraps it.
	bool FindFormRecursive(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk, FSWGIffTag TargetFormType, FSWGIffChunk& OutForm, int32 MaxDepth)
	{
		if (!Chunk.IsForm())
		{
			return false;
		}
		if (Chunk.FormType == TargetFormType)
		{
			OutForm = Chunk;
			return true;
		}
		if (MaxDepth <= 0)
		{
			return false;
		}
		for (const FSWGIffChunk& Child : Reader.ReadChildren(Chunk))
		{
			if (Child.IsForm() && FindFormRecursive(Reader, Child, TargetFormType, OutForm, MaxDepth - 1))
			{
				return true;
			}
		}
		return false;
	}

	// FORM SWTS > FORM 0000 > [CHUNK NAME (fallback/base shader reference,
	// not needed), FORM DTST (per-frame timing, not needed), then a flat list
	// of CHUNK TEXT — each one is a reversed-fourCC usage tag ("MAIN", etc.)
	// directly concatenated with the frame's texture path, no separate DATA/
	// NAME split like FORM SSHT's TXM entries use. Picks the first MAIN-tagged
	// frame (falling back to the very first TEXT chunk) as a static
	// approximation of the animation — true frame animation is out of scope.
	FString ResolveAnimatedShaderDiffuseTexturePath(const FSWGIffReader& Reader, const FSWGIffChunk& SwtsForm)
	{
		const TArray<FSWGIffChunk> VersionForms = Reader.FindChildForms(SwtsForm);
		if (VersionForms.Num() == 0)
		{
			return FString();
		}

		FString FirstTexturePath, MainTexturePath;

		for (const FSWGIffChunk& Child : Reader.ReadChildren(VersionForms[0]))
		{
			if (Child.IsForm() || Child.Tag != SWG_IFF_TAG('T','E','X','T'))
			{
				continue;
			}

			const uint8* D = Reader.GetChunkData(Child);
			const int32 Size = Reader.GetChunkSize(Child);
			if (Size <= 4)
			{
				continue;
			}

			const FString Tag = FString::Printf(TEXT("%c%c%c%c"), (TCHAR)D[3], (TCHAR)D[2], (TCHAR)D[1], (TCHAR)D[0]);
			const FString TexturePath = FString::ConstructFromPtrSize((const ANSICHAR*)(D + 4), Size - 4 - 1); // -1: trailing null

			if (TexturePath.IsEmpty())
			{
				continue;
			}
			if (FirstTexturePath.IsEmpty())
			{
				FirstTexturePath = TexturePath;
			}
			if (Tag == TEXT("MAIN") && MainTexturePath.IsEmpty())
			{
				MainTexturePath = TexturePath;
			}
		}

		return !MainTexturePath.IsEmpty() ? MainTexturePath : FirstTexturePath;
	}
}

FString USWGMeshGeneratorSubsystem::ResolveShaderDiffuseTexturePath(const FString& ShaderVirtualPath)
{
	if (ShaderVirtualPath.IsEmpty() || !TreSubsystem)
	{
		return FString();
	}

	FSWGIffReader Reader = TreSubsystem->CreateIffReader(ShaderVirtualPath);
	if (!Reader.IsValid())
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: failed to open shader %s"), *ShaderVirtualPath);
		return FString();
	}

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0)
	{
		return FString();
	}

	FSWGIffChunk SshtForm;
	if (!FindFormRecursive(Reader, TopLevel[0], SWG_IFF_TAG('S','S','H','T'), SshtForm, 3))
	{
		FSWGIffChunk SwtsForm;
		if (FindFormRecursive(Reader, TopLevel[0], SWG_IFF_TAG('S','W','T','S'), SwtsForm, 3))
		{
			FString Result = ResolveAnimatedShaderDiffuseTexturePath(Reader, SwtsForm);
			Result.ReplaceInline(TEXT("\\"), TEXT("/"));
			return Result;
		}

		UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: shader %s has no SSHT or SWTS form (top-level FORM %s)"), *ShaderVirtualPath, *TopLevel[0].FormType.ToString());
		return FString();
	}

	// SSHT's version-tagged wrapper form isn't always "0000" (same version-drift
	// pattern already hit for .trn's PTAT and .msh's MESH forms) — take
	// whichever single FORM child is actually present rather than hardcode it.
	const TArray<FSWGIffChunk> SshtChildForms = Reader.FindChildForms(SshtForm);
	if (SshtChildForms.Num() == 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: shader %s SSHT has no version form"), *ShaderVirtualPath);
		return FString();
	}
	const FSWGIffChunk& Form0000 = SshtChildForms[0];

	FSWGIffChunk TxmsForm;
	if (!Reader.FindChildForm(Form0000, SWG_IFF_TAG('T','X','M','S'), TxmsForm))
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: shader %s (version form %s) has no TXMS"), *ShaderVirtualPath, *Form0000.FormType.ToString());
		return FString();
	}

	FString FirstTexturePath, MainTexturePath;

	for (const FSWGIffChunk& TxmForm : Reader.ReadChildren(TxmsForm))
	{
		if (!TxmForm.IsForm() || TxmForm.FormType != SWG_IFF_TAG('T','X','M',' '))
		{
			continue;
		}

		FSWGIffChunk InnerForm0001, DataChunk, NameChunk;
		if (!Reader.FindChildForm(TxmForm, SWG_IFF_TAG('0','0','0','1'), InnerForm0001)) continue;
		if (!Reader.FindChildChunk(InnerForm0001, SWGIffTags::Data, DataChunk)) continue;
		if (!Reader.FindChildChunk(InnerForm0001, SWGIffTags::Name, NameChunk)) continue;

		const FString TexturePath = ReadFullChunkString(Reader, NameChunk);
		if (TexturePath.IsEmpty())
		{
			continue;
		}

		if (FirstTexturePath.IsEmpty())
		{
			FirstTexturePath = TexturePath;
		}

		const uint8* D = Reader.GetChunkData(DataChunk);
		if (Reader.GetChunkSize(DataChunk) >= 4)
		{
			// The usage tag ("MAIN", "SPEC", ...) is stored reversed byte-for-byte
			// (confirmed live: 4E 49 41 4D == 'N','I','A','M' == "MAIN" reversed).
			const FString Tag = FString::Printf(TEXT("%c%c%c%c"), (TCHAR)D[3], (TCHAR)D[2], (TCHAR)D[1], (TCHAR)D[0]);
			if (Tag == TEXT("MAIN"))
			{
				MainTexturePath = TexturePath;
			}
		}
	}

	// Path separators are inconsistent in real shader data (e.g.
	// "texture\specular_lookup_05.dds" alongside "texture/hum_m_body.dds" in
	// the very same file) — TRE virtual paths are always forward-slash.
	FString Result = !MainTexturePath.IsEmpty() ? MainTexturePath : FirstTexturePath;
	Result.ReplaceInline(TEXT("\\"), TEXT("/"));
	return Result;
}

FString USWGMeshGeneratorSubsystem::ResolveShaderTintPalettePath(const FString& ShaderVirtualPath)
{
	if (ShaderVirtualPath.IsEmpty() || !TreSubsystem)
	{
		return FString();
	}

	FSWGIffReader Reader = TreSubsystem->CreateIffReader(ShaderVirtualPath);
	if (!Reader.IsValid())
	{
		return FString();
	}

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0)
	{
		return FString();
	}

	// Unlike TXMS (nested inside SSHT > FORM 0000), TFAC is a sibling of
	// FORM SSHT itself — both direct children of the outer FORM 0001 wrapper
	// — confirmed via a live ifftree dump of shader/wke_m_body.sht (TFAC and
	// TXTR sit at the same indentation as SSHT, not inside it). Searching
	// under SSHT's own version form here (as ResolveShaderDiffuseTexturePath
	// does for TXMS) would never find it — hit exactly that bug first try
	// (TintColor silently stayed at its default white, no visible change).
	FSWGIffChunk TfacForm;
	if (!FindFormRecursive(Reader, TopLevel[0], SWG_IFF_TAG('T','F','A','C'), TfacForm, 4))
	{
		// No customization palette on this shader — normal for plain object/weapon/building shaders.
		return FString();
	}

	FString FirstPalettePath, MainPalettePath;

	for (const FSWGIffChunk& PalChunk : Reader.ReadChildren(TfacForm))
	{
		if (PalChunk.IsForm() || PalChunk.Tag != SWG_IFF_TAG('P','A','L',' '))
		{
			continue;
		}

		const uint8* Data = Reader.GetChunkData(PalChunk);
		const int32 Size = Reader.GetChunkSize(PalChunk);

		// [key CString][null terminator][1 extra flag byte, unexplored]
		// [4-byte reversed tag][palette path CString][trailing bytes] — note
		// two null bytes separate the key from the tag, not one. The key is
		// the customization variable's own bare name (e.g. "index_color_1"),
		// matching FSWGCustomizationVariableDef::Name's tail — confirmed via
		// live IFF dump of shader/wke_m_body.sht, which has two PAL entries,
		// "index_color_1" and "index_color_3", BOTH pointing at the same
		// palette/pc_skin_wke.pal and NEITHER tagged "MAIN" (that tag/Tag
		// pairing turned out to be for a different shader family — every
		// creature skin shader checked has it empty/non-MAIN on both
		// entries). Falling back to "MAIN", as this function used to
		// exclusively do, left the choice between the two effectively
		// TMap-iteration-order-dependent (via ResolveCustomizationPaletteTints,
		// which keys its result by palette *path* — a second variable sharing
		// that same path silently overwrites the first). Explicitly preferring
		// the entry keyed "index_color_1" — the conventional primary skin/fur
		// tone variable across every creature template checked — makes this
		// deterministic instead.
		const int32 KeyStart = 0;
		int32 Offset = KeyStart;
		while (Offset < Size && Data[Offset] != 0) ++Offset;
		const FString Key(Offset - KeyStart, (const ANSICHAR*)(Data + KeyStart));
		if (Offset >= Size) continue;
		Offset += 2;

		if (Offset + 4 > Size) continue;
		const FString Tag = FString::Printf(TEXT("%c%c%c%c"), (TCHAR)Data[Offset + 3], (TCHAR)Data[Offset + 2], (TCHAR)Data[Offset + 1], (TCHAR)Data[Offset + 0]);
		Offset += 4;

		const int32 PathStart = Offset;
		while (Offset < Size && Data[Offset] != 0) ++Offset;
		if (Offset <= PathStart) continue;

		FString PalettePath(Offset - PathStart, (const ANSICHAR*)(Data + PathStart));
		if (PalettePath.IsEmpty()) continue;

		if (FirstPalettePath.IsEmpty())
		{
			FirstPalettePath = PalettePath;
		}
		if (Tag == TEXT("MAIN") && MainPalettePath.IsEmpty())
		{
			MainPalettePath = PalettePath;
		}
		if (Key == TEXT("index_color_1"))
		{
			MainPalettePath = PalettePath; // Highest priority — always wins if present.
		}
	}

	FString Result = !MainPalettePath.IsEmpty() ? MainPalettePath : FirstPalettePath;
	Result.ReplaceInline(TEXT("\\"), TEXT("/"));
	return Result;
}

USWGMeshGeneratorSubsystem::FSWGShaderCustomization USWGMeshGeneratorSubsystem::ResolveShaderCustomization(const FString& ShaderVirtualPath)
{
	FSWGShaderCustomization Result;
	if (ShaderVirtualPath.IsEmpty() || !TreSubsystem)
	{
		return Result;
	}

	FSWGIffReader Reader = TreSubsystem->CreateIffReader(ShaderVirtualPath);
	if (!Reader.IsValid())
	{
		return Result;
	}

	const TArray<FSWGIffChunk> TopLevel = Reader.ReadChunks();
	if (TopLevel.Num() == 0)
	{
		return Result;
	}

	// FORM TFAC > CHUNK PAL (repeated): [varName\0][\0][4-byte reversed
	// color-factor tag][palette path\0]. Same byte layout
	// ResolveShaderTintPalettePath parses — see its comment — but here the
	// factor tag is the point rather than the palette path.
	FSWGIffChunk TfacForm;
	if (FindFormRecursive(Reader, TopLevel[0], SWG_IFF_TAG('T','F','A','C'), TfacForm, 4))
	{
		for (const FSWGIffChunk& PalChunk : Reader.ReadChildren(TfacForm))
		{
			if (PalChunk.IsForm() || PalChunk.Tag != SWG_IFF_TAG('P','A','L',' '))
			{
				continue;
			}

			const uint8* Data = Reader.GetChunkData(PalChunk);
			const int32 Size = Reader.GetChunkSize(PalChunk);

			int32 Offset = 0;
			while (Offset < Size && Data[Offset] != 0) ++Offset;
			const FString Key(Offset, (const ANSICHAR*)Data);
			Offset += 2; // the key's own null, plus one flag byte
			if (Key.IsEmpty() || Offset + 4 > Size)
			{
				continue;
			}

			const FString FactorTag = FString::Printf(TEXT("%c%c%c%c"),
				(TCHAR)Data[Offset + 3], (TCHAR)Data[Offset + 2], (TCHAR)Data[Offset + 1], (TCHAR)Data[Offset]);
			Result.ColorFactorTags.Add(Key, FactorTag);
		}
	}

	// FORM TXTR > CHUNK DATA: [uint16 count][count * texture path\0] — the
	// explicit ordered variant list an index_texture* variable selects from.
	// FORM TXTR > FORM CUST > CHUNK TX1D: [4-byte reversed slot tag]
	// [uint16][uint16 count][varName\0] names that variable.
	FSWGIffChunk TxtrForm;
	if (FindFormRecursive(Reader, TopLevel[0], SWG_IFF_TAG('T','X','T','R'), TxtrForm, 4))
	{
		FSWGIffChunk DataChunk;
		if (Reader.FindChildChunk(TxtrForm, SWGIffTags::Data, DataChunk))
		{
			const uint8* Data = Reader.GetChunkData(DataChunk);
			const int32 Size = Reader.GetChunkSize(DataChunk);
			const int32 Count = Size >= 2 ? ((int32)Data[0] | ((int32)Data[1] << 8)) : 0;
			int32 Offset = 2;
			for (int32 i = 0; i < Count && Offset < Size; ++i)
			{
				const int32 Start = Offset;
				while (Offset < Size && Data[Offset] != 0) ++Offset;
				FString Path = FString::ConstructFromPtrSize((const ANSICHAR*)(Data + Start), Offset - Start);
				Path.ReplaceInline(TEXT("\\"), TEXT("/"));
				Result.TextureVariants.Add(MoveTemp(Path));
				++Offset; // past the terminator
			}
		}

		FSWGIffChunk CustForm;
		if (Reader.FindChildForm(TxtrForm, SWG_IFF_TAG('C','U','S','T'), CustForm))
		{
			for (const FSWGIffChunk& Child : Reader.ReadChildren(CustForm))
			{
				if (Child.IsForm()) continue;
				const uint8* Data = Reader.GetChunkData(Child);
				const int32 Size = Reader.GetChunkSize(Child);
				if (Size < 9) continue;
				int32 Offset = 8;
				const int32 Start = Offset;
				while (Offset < Size && Data[Offset] != 0) ++Offset;
				const FString Var = FString::ConstructFromPtrSize((const ANSICHAR*)(Data + Start), Offset - Start);
				if (!Var.IsEmpty())
				{
					Result.TextureVariantVariable = Var;
					break; // Every creature shader checked binds exactly one.
				}
			}
		}
	}

	return Result;
}

namespace
{
	// Shared header parse for both LoadPaletteAverageTint and
	// LoadPaletteColorAtIndex — standard little-endian RIFF "PAL " palette
	// file, distinct from every other SWG format read in this codebase (all
	// big-endian IFF). Layout: "RIFF"(4) + size(4) + "PAL "(4) + "data"(4) +
	// chunkSize(4) + PALETTEHEADER{u16 Version; u16 NumEntries;} +
	// NumEntries*{R,G,B,Flags}. Returns the byte offset of the first entry
	// and the entry count, or false if the file doesn't parse as this format.
	bool ParsePaletteHeader(const TArray<uint8>& Bytes, int32& OutFirstEntryOffset, uint16& OutNumEntries)
	{
		if (Bytes.Num() < 20 || Bytes[0] != 'R' || Bytes[1] != 'I' || Bytes[2] != 'F' || Bytes[3] != 'F')
		{
			return false;
		}

		int32 Offset = 12; // past "RIFF" + size + "PAL "
		Offset += 8; // past "data" + chunk size
		if (Offset + 4 > Bytes.Num())
		{
			return false;
		}

		OutNumEntries = Bytes[Offset + 2] | (Bytes[Offset + 3] << 8);
		Offset += 4;

		if (OutNumEntries == 0 || Offset + (int32)OutNumEntries * 4 > Bytes.Num())
		{
			return false;
		}

		OutFirstEntryOffset = Offset;
		return true;
	}
}

FLinearColor USWGMeshGeneratorSubsystem::LoadPaletteAverageTint(const FString& PaletteVirtualPath)
{
	if (PaletteVirtualPath.IsEmpty() || !TreSubsystem || !TreSubsystem->FileExists(PaletteVirtualPath))
	{
		return FLinearColor::White;
	}

	const TArray<uint8> Bytes = TreSubsystem->ExtractFile(PaletteVirtualPath);

	int32 Offset = 0;
	uint16 NumEntries = 0;
	if (!ParsePaletteHeader(Bytes, Offset, NumEntries))
	{
		return FLinearColor::White;
	}

	FVector Sum = FVector::ZeroVector;
	for (uint16 i = 0; i < NumEntries; ++i)
	{
		const int32 EntryOffset = Offset + i * 4;
		Sum += FVector(Bytes[EntryOffset], Bytes[EntryOffset + 1], Bytes[EntryOffset + 2]);
	}

	const FVector Average = (Sum / (double)NumEntries) / 255.0;
	return FLinearColor(Average.X, Average.Y, Average.Z);
}

FLinearColor USWGMeshGeneratorSubsystem::LoadPaletteColorAtIndex(const FString& PaletteVirtualPath, int32 Index)
{
	if (PaletteVirtualPath.IsEmpty() || !TreSubsystem || !TreSubsystem->FileExists(PaletteVirtualPath))
	{
		return FLinearColor::White;
	}

	const TArray<uint8> Bytes = TreSubsystem->ExtractFile(PaletteVirtualPath);

	int32 Offset = 0;
	uint16 NumEntries = 0;
	if (!ParsePaletteHeader(Bytes, Offset, NumEntries))
	{
		return FLinearColor::White;
	}

	const int32 ClampedIndex = FMath::Clamp(Index, 0, (int32)NumEntries - 1);
	const int32 EntryOffset = Offset + ClampedIndex * 4;
	return FLinearColor(FColor(Bytes[EntryOffset], Bytes[EntryOffset + 1], Bytes[EntryOffset + 2], 255));
}

void USWGMeshGeneratorSubsystem::EnsureCustomizationManagersLoaded()
{
	if (bCustomizationManagersLoaded)
	{
		return;
	}
	bCustomizationManagersLoaded = true; // Only try once per session either way — a missing/corrupt table isn't going to fix itself mid-session.
	if (!FSWGCustomizationIdManager::Read(TreSubsystem->CreateIffReader(TEXT("customization/customization_id_manager.iff")), CustomizationIdManager))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to read customization_id_manager.iff — customization won't apply this session"));
	}
	if (!AssetCustomizationManager.Read(TreSubsystem->CreateIffReader(TEXT("customization/asset_customization_manager.iff"))))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to read asset_customization_manager.iff — customization won't apply this session"));
	}
}

TMap<FString, FLinearColor> USWGMeshGeneratorSubsystem::ResolveCustomizationPaletteTints(const FSWGCustomizationVariables& Customization, const FString& AppearancePath)
{
	TMap<FString, FLinearColor> Result;
	EnsureCustomizationManagersLoaded();

	TMap<FString, FSWGCustomizationVariableDef> Variables;
	AssetCustomizationManager.GetCustomizationVariables(AppearancePath, false, Variables);

	for (const TPair<FString, FSWGCustomizationVariableDef>& Pair : Variables)
	{
		const FSWGCustomizationVariableDef& Def = Pair.Value;
		if (!Def.bIsPalette || Def.PaletteFileName.IsEmpty())
		{
			continue;
		}

		const uint8* TypeId = CustomizationIdManager.NameToId.Find(Def.Name);
		if (!TypeId)
		{
			continue;
		}

		const int16* Value = Customization.Values.Find(*TypeId);
		if (!Value)
		{
			continue; // This object never customized this particular variable — leave it at whatever the shader's own default/average gives.
		}

		// Keyed by the variable's own bare name (e.g. "index_color_1"), not
		// its palette file — SWG's two-tone creature coloring (a Wookiee's
		// light/dark fur mix, an alien's body-vs-limb split) comes from TWO
		// simultaneously-active palette variables, index_color_1 (primary)
		// and index_color_3 (secondary), that both reference the SAME
		// palette file on every creature template checked. Keying by file
		// path (as this used to) meant one silently overwrote the other;
		// GetOrBuildObjectMaterial now looks both up by name directly and
		// blends them via the mesh's own per-vertex color (see
		// FSWGSkeletalMeshImporter::PopulateImportData's bHasVertexColors
		// comment).
		FString BareName = Def.Name;
		int32 SlashIndex = INDEX_NONE;
		if (BareName.FindLastChar(TEXT('/'), SlashIndex))
		{
			BareName = BareName.Mid(SlashIndex + 1);
		}
		Result.Add(BareName, LoadPaletteColorAtIndex(Def.PaletteFileName, *Value));
	}

	return Result;
}

TMap<FString, float> USWGMeshGeneratorSubsystem::ResolveCustomizationMorphWeights(const FSWGCustomizationVariables& Customization, const FString& AppearancePath)
{
	TMap<FString, float> Result;
	EnsureCustomizationManagersLoaded();

	TMap<FString, FSWGCustomizationVariableDef> Variables;
	AssetCustomizationManager.GetCustomizationVariables(AppearancePath, false, Variables);

	for (const TPair<FString, FSWGCustomizationVariableDef>& Pair : Variables)
	{
		const FSWGCustomizationVariableDef& Def = Pair.Value;
		// Only "blend_*" RANGE variables are mesh deformations (they name a
		// .mgn FORM BLTS morph target directly — see
		// FSWGMeshReader::ReadBlendTargets). Other RANGE variables select
		// texture variants or styles and are handled elsewhere; matching on
		// the blend_ prefix rather than excluding known non-morph prefixes
		// means a new style/index variable can't silently be misread as a
		// morph target.
		if (Def.bIsPalette || Def.MaxValue <= Def.MinValue || !Def.Name.Contains(TEXT("blend_")))
		{
			continue;
		}

		const uint8* TypeId = CustomizationIdManager.NameToId.Find(Def.Name);
		if (!TypeId)
		{
			continue;
		}

		const int16* Value = Customization.Values.Find(*TypeId);
		if (!Value)
		{
			continue; // Never customized — leave this shape at the mesh's built-in default (zero weight).
		}

		const float Normalized = FMath::Clamp((float)(*Value - Def.MinValue) / (float)(Def.MaxValue - Def.MinValue), 0.0f, 1.0f);

		// Def.Name is the full customization-variable path (e.g.
		// "/shared_owner/blend_fat"); the .mgn's own FORM BLTS morph target
		// name (see FSWGMeshReader::ReadBlendTargets) is just the bare
		// "blend_fat" tail — strip everything up to and including the last '/'.
		FString MorphTargetName = Def.Name;
		int32 SlashIndex = INDEX_NONE;
		if (MorphTargetName.FindLastChar(TEXT('/'), SlashIndex))
		{
			MorphTargetName = MorphTargetName.Mid(SlashIndex + 1);
		}

		Result.Add(MorphTargetName, Normalized);
	}

	return Result;
}

TMap<FString, int32> USWGMeshGeneratorSubsystem::ResolveCustomizationTextureIndices(const FSWGCustomizationVariables& Customization, const FString& AppearancePath)
{
	TMap<FString, int32> Result;
	EnsureCustomizationManagersLoaded();

	TMap<FString, FSWGCustomizationVariableDef> Variables;
	AssetCustomizationManager.GetCustomizationVariables(AppearancePath, false, Variables);

	for (const TPair<FString, FSWGCustomizationVariableDef>& Pair : Variables)
	{
		const FSWGCustomizationVariableDef& Def = Pair.Value;
		if (Def.bIsPalette)
		{
			continue;
		}

		// No name filtering — which variable actually selects a texture is
		// declared by the shader's own FORM CUST > TX1D binding (see
		// ResolveShaderCustomization), so just surface every non-palette
		// variable by bare name and let GetOrBuildObjectMaterial pick.
		FString BareName = Def.Name;
		int32 SlashIndex = INDEX_NONE;
		if (BareName.FindLastChar(TEXT('/'), SlashIndex))
		{
			BareName = BareName.Mid(SlashIndex + 1);
		}

		const uint8* TypeId = CustomizationIdManager.NameToId.Find(Def.Name);
		if (!TypeId)
		{
			continue;
		}

		const int16* Value = Customization.Values.Find(*TypeId);
		if (!Value)
		{
			continue; // Never customized — leave the shader's baked-in default variant.
		}

		Result.Add(BareName, *Value);
	}

	return Result;
}

UTexture2D* USWGMeshGeneratorSubsystem::GetOrLoadObjectTexture(const FString& TextureVirtualPath, bool bSRGB,
	bool bLegacyDXT5Normal)
{
	if (TextureVirtualPath.IsEmpty())
	{
		return nullptr;
	}

	const FString CacheKey = FString::Printf(TEXT("%s|%s|%s"), *TextureVirtualPath,
		bSRGB ? TEXT("sRGB") : TEXT("linear"), bLegacyDXT5Normal ? TEXT("dxt5nm") : TEXT("native"));
	if (TObjectPtr<UTexture2D>* Existing = LoadedObjectTextures.Find(CacheKey))
	{
		return *Existing;
	}

	UTexture2D* Result = nullptr;
	if (TreSubsystem && TreSubsystem->FileExists(TextureVirtualPath))
	{
		const TArray<uint8> Bytes = TreSubsystem->ExtractFile(TextureVirtualPath);
		Result = FSWGDDSTextureLoader::LoadTexture2D(Bytes, FName(*TextureVirtualPath), bSRGB, bLegacyDXT5Normal);
	}

	if (!Result)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to load object texture '%s'"), *TextureVirtualPath);
	}

	LoadedObjectTextures.Add(CacheKey, Result);
	return Result;
}

UMaterialInterface* USWGMeshGeneratorSubsystem::GetOrBuildObjectMaterial(const FString& ShaderVirtualPath, const TMap<FString, FLinearColor>* PaletteTintOverrides, const TMap<FString, int32>* TextureIndexOverrides)
{
	if (ShaderVirtualPath.IsEmpty())
	{
		return nullptr;
	}

	// Everything about how this shader consumes customization comes from the
	// shader itself (see ResolveShaderCustomization): which variable feeds
	// the MAIN color factor, which feeds HUEB, and which variable indexes
	// its explicit texture-variant list. No per-species naming assumptions.
	// The two colors are blended by the material (the Diffuse texture's own
	// alpha channel drives a Lerp between TintColor/TintColor2 — see
	// M_SWGObjectTextured's graph).
	const bool bHasAnyOverrides =
		(PaletteTintOverrides && PaletteTintOverrides->Num() > 0) ||
		(TextureIndexOverrides && TextureIndexOverrides->Num() > 0);

	FString PalettePath;
	const FLinearColor* TintOverride = nullptr;
	const FLinearColor* TintOverride2 = nullptr;
	const int32* TextureIndexOverride = nullptr;
	FSWGShaderCustomization ShaderCustomization;
	if (bHasAnyOverrides)
	{
		ShaderCustomization = ResolveShaderCustomization(ShaderVirtualPath);

		if (PaletteTintOverrides && PaletteTintOverrides->Num() > 0)
		{
			const FString MainVariable = ShaderCustomization.FindVariableForFactor(TEXT("MAIN"));
			const FString HueVariable = ShaderCustomization.FindVariableForFactor(TEXT("HUEB"));
			if (!MainVariable.IsEmpty())
			{
				TintOverride = PaletteTintOverrides->Find(MainVariable);
			}
			if (!HueVariable.IsEmpty())
			{
				TintOverride2 = PaletteTintOverrides->Find(HueVariable);
			}
		}

		if (TextureIndexOverrides && TextureIndexOverrides->Num() > 0 && !ShaderCustomization.TextureVariantVariable.IsEmpty())
		{
			TextureIndexOverride = TextureIndexOverrides->Find(ShaderCustomization.TextureVariantVariable);
		}
	}

	// A tinted/variant-overridden material is specific to this one actor's
	// customization, not reusable shader-wide — bypass the shared cache
	// entirely for it (both read and, further below, write).
	if (!TintOverride && !TintOverride2 && !TextureIndexOverride)
	{
		if (TObjectPtr<UMaterialInterface>* Existing = ObjectMaterialCache.Find(ShaderVirtualPath))
		{
			return *Existing;
		}
	}

	UMaterialInterface* Result = nullptr;
	FSWGShaderData ShaderData;
	FSWGShaderReader::ReadShader(TreSubsystem->CreateIffReader(ShaderVirtualPath), ShaderData);

	const FSWGShaderTexture* DiffuseDef = ShaderData.FindTexture(ESWGShaderTextureUsage::Diffuse);
	const FSWGShaderTexture* NormalDef = ShaderData.FindTexture(ESWGShaderTextureUsage::Normal);
	const FSWGShaderTexture* SpecularDef = ShaderData.FindTexture(ESWGShaderTextureUsage::Specular);

	// SWTS animated shaders and unusual legacy templates still use the older
	// diffuse resolver as a fallback.
	FString TexturePath = DiffuseDef ? DiffuseDef->VirtualPath : ResolveShaderDiffuseTexturePath(ShaderVirtualPath);
	// The selected variant is a plain 0-based index into the shader's own
	// ordered TXTR list — NOT a "_sNN" suffix rewrite of the default path,
	// which an earlier version did and got wrong by one (index 16 of the
	// Wookiee's 25-entry list is wke_pattern_s17.dds, not _s16).
	if (TextureIndexOverride && ShaderCustomization.TextureVariants.IsValidIndex(*TextureIndexOverride))
	{
		TexturePath = ShaderCustomization.TextureVariants[*TextureIndexOverride];
	}
	UTexture2D* Texture = GetOrLoadObjectTexture(TexturePath, /*bSRGB=*/true);
	UTexture2D* NormalTexture = NormalDef ? GetOrLoadObjectTexture(NormalDef->VirtualPath, /*bSRGB=*/false,
		/*bLegacyDXT5Normal=*/NormalDef->Tag.Equals(TEXT("CNRM"), ESearchCase::IgnoreCase)) : nullptr;
	UTexture2D* SpecularTexture = SpecularDef ? GetOrLoadObjectTexture(SpecularDef->VirtualPath, /*bSRGB=*/false) : nullptr;

	if (!Texture)
	{
		// Diagnostic: pins down which shaders' texture resolution silently fails
		// inside ResolveShaderDiffuseTexturePath's TXM-tag loop.
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: no diffuse texture resolved for shader '%s' (resolved texture path: '%s')"),
			*ShaderVirtualPath, *TexturePath);
	}

	if (Texture)
	{
		if (!ObjectMaterialParent)
		{
			ObjectMaterialParent = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/SWGEmu/Materials/M_SWGObjectTextured.M_SWGObjectTextured"));
		}
		if (!ObjectMaterialParentMasked)
		{
			ObjectMaterialParentMasked = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/SWGEmu/Materials/M_SWGObjectTexturedMasked.M_SWGObjectTexturedMasked"));
		}

		// SWG's "alpha" shaders (fur/hair overlay cards, foliage, etc. — see
		// FSWGShaderData::NeedsAlphaBlend) render as alpha-masked, not opaque;
		// using the opaque parent for these leaves their cutout geometry
		// fully solid, showing as a hard-edged silhouette instead of blending
		// into the surface underneath.
		UMaterialInterface* Parent = (ShaderData.NeedsAlphaBlend() && ObjectMaterialParentMasked)
			? ObjectMaterialParentMasked.Get()
			: ObjectMaterialParent.Get();

		if (Parent)
		{
			if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this))
			{
				MID->SetTextureParameterValue(TEXT("Diffuse"), Texture);
				if (NormalTexture)
				{
					MID->SetTextureParameterValue(TEXT("Normal"), NormalTexture);
				}
				if (SpecularTexture)
				{
					MID->SetTextureParameterValue(TEXT("Specular"), SpecularTexture);
				}

				// Creature/player skins are a pattern texture meant to be
				// tinted by a customization palette, not a ready-to-use
				// diffuse on its own (see ResolveShaderTintPalettePath).
				// TintOverride/TintOverride2 (see above) are the real
				// per-character index_color_1/index_color_3 picks when
				// available; otherwise fall back to the palette's flat
				// average color for both, as before this two-tone mechanism
				// was understood (no per-vertex variation without a real
				// per-character customization to drive it anyway).
				if (TintOverride)
				{
					MID->SetVectorParameterValue(TEXT("TintColor"), *TintOverride);
					MID->SetVectorParameterValue(TEXT("TintColor2"), TintOverride2 ? *TintOverride2 : *TintOverride);
				}
				else
				{
					if (PalettePath.IsEmpty())
					{
						PalettePath = ResolveShaderTintPalettePath(ShaderVirtualPath);
					}
					if (!PalettePath.IsEmpty())
					{
						const FLinearColor AverageTint = LoadPaletteAverageTint(PalettePath);
						MID->SetVectorParameterValue(TEXT("TintColor"), AverageTint);
						MID->SetVectorParameterValue(TEXT("TintColor2"), AverageTint);
					}
				}

				Result = MID;
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: M_SWGObjectTextured not found — falling back to tint material for shader '%s'"), *ShaderVirtualPath);
		}
	}

	if (!TintOverride && !TintOverride2 && !TextureIndexOverride)
	{
		ObjectMaterialCache.Add(ShaderVirtualPath, Result);
	}
	return Result;
}

UStaticMesh* USWGMeshGeneratorSubsystem::GetOrBuildGeneratedStaticMesh(uint32 CacheHash, const FString& DebugName, const FSWGMeshData& MeshData, const FVector3f& PlaceholderColor)
{
	const FString AssetName = FString::Printf(TEXT("SM_%u"), CacheHash);
	const FString PackagePath = TEXT("/Game/SWGEmu/Generated/") + AssetName;

	// The saved .uasset on disk is the cache, same role it plays for
	// GetOrBuildGeneratedSkeletalMesh — one static mesh asset shared by every
	// actor whose CacheHash matches (world-snapshot objects: every placed
	// instance of one template, since its geometry never varies per-instance;
	// creatures/players/items: every instance sharing the same resolved mesh
	// file(s) + actor class). A hit here needs no MeshData at all — pure
	// geometry, no parse/build work.
	if (UStaticMesh* Existing = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName)))
	{
		return Existing;
	}

#if WITH_EDITOR
	if (MeshData.Submeshes.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: no submeshes to build a generated static mesh for '%s'"), *DebugName);
		return nullptr;
	}

	// A bare, unregistered, never-rendered UDynamicMesh — just a scratch
	// buffer to hand to CopyMeshToStaticMesh, not a component. Building a
	// full UDynamicMeshComponent (register, attach, material-assign, later
	// destroy) here would be pure waste: this path only runs on a cache
	// miss, and the component would never be shown.
	UDynamicMesh* ScratchMesh = NewObject<UDynamicMesh>(GetTransientPackage());
	ScratchMesh->EditMesh([&MeshData, PlaceholderColor](FDynamicMesh3& EditMesh)
	{
		PopulateDynamicMeshFromMeshData(EditMesh, MeshData, PlaceholderColor);
	});

	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();
	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Package, FName(*AssetName), RF_Public | RF_Standalone);

	// Deliberately NOT baking real per-shader materials into the asset here
	// (no Options.bReplaceMaterials/NewMaterials): those are
	// UMaterialInstanceDynamic instances outered to this subsystem, not
	// assets in this package — saving that reference into a persistent
	// .uasset doesn't survive a reload (the MID isn't itself
	// package-resident), which is what left every cached static mesh
	// untextured after the first session. Real materials are assigned at
	// runtime on the component instead (see BuildGeneratedMeshComponent) —
	// same pattern GetOrBuildGeneratedSkeletalMesh already uses
	// (TryApplyGeneratedAnimatedMesh's per-slot CharacterMesh->SetMaterial
	// calls, never baked into the saved USkeletalMesh either).
	// CommitMeshDescription (inside CopyMeshToStaticMesh) still auto-creates
	// one material slot per submesh from the mesh description's polygon
	// groups, which is all that runtime SetMaterial call needs to succeed.
	FGeometryScriptCopyMeshToAssetOptions Options;
	Options.bEmitTransaction = false;

	FGeometryScriptMeshWriteLOD TargetLOD;
	EGeometryScriptOutcomePins Outcome;
	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(ScratchMesh, StaticMesh, Options, TargetLOD, Outcome);

	if (Outcome != EGeometryScriptOutcomePins::Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: CopyMeshToStaticMesh failed for '%s'"), *DebugName);
		return nullptr;
	}

	StaticMesh->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(StaticMesh);

	const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	UPackage::SavePackage(Package, StaticMesh, *FileName, SaveArgs);

	UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: built and cached generated static mesh '%s' for '%s'"), *PackagePath, *DebugName);
	return StaticMesh;
#else
	UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: generated static mesh '%s' isn't built yet and can't be built in a packaged build"), *PackagePath);
	return nullptr;
#endif
}

UMeshComponent* USWGMeshGeneratorSubsystem::BuildGeneratedMeshComponent(AActor& Actor, const FSWGMeshData& MeshData, uint32 CacheHash, const FString& DebugName, float YawCorrectionDegrees, EComponentMobility::Type Mobility, const TMap<FString, FLinearColor>* PaletteTintOverrides, const TMap<FString, int32>* TextureIndexOverrides)
{
	const FVector3f PlaceholderColor = GetPlaceholderColorForActor(Actor);
	UStaticMesh* StaticMesh = GetOrBuildGeneratedStaticMesh(CacheHash, DebugName, MeshData, PlaceholderColor);
	if (!StaticMesh)
	{
		return nullptr;
	}

	UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(&Actor, NAME_None, RF_Transactional);
	MeshComponent->SetStaticMesh(StaticMesh);

	// Real per-shader materials, assigned live on the component every time
	// (cheap — GetOrBuildObjectMaterial's own cache means this is just a
	// lookup, not a rebuild) rather than baked into the cached asset — see
	// GetOrBuildGeneratedStaticMesh for why. Falls back to the engine's
	// vertex-color debug material for any submesh whose shader
	// failed/unresolved, so PlaceholderColor (baked into the mesh's vertex
	// colors) is still visible for it instead of the flat default material.
	UMaterialInterface* PlaceholderMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/EngineDebugMaterials/VertexColorViewMode_ColorOnly.VertexColorViewMode_ColorOnly"));
	if (!PlaceholderMaterial)
	{
		PlaceholderMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	int32 NumTextured = 0;
	for (int32 i = 0; i < MeshData.Submeshes.Num(); ++i)
	{
		UMaterialInterface* Mat = GetOrBuildObjectMaterial(MeshData.Submeshes[i].ShaderName, PaletteTintOverrides, TextureIndexOverrides);
		if (Mat)
		{
			++NumTextured;
		}
		MeshComponent->SetMaterial(i, Mat ? Mat : PlaceholderMaterial);
	}
	UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: %s (class %s) assigned %d/%d real textured material(s), tint=(%.2f,%.2f,%.2f)"),
		*Actor.GetName(), *Actor.GetClass()->GetName(), NumTextured, MeshData.Submeshes.Num(),
		PlaceholderColor.X, PlaceholderColor.Y, PlaceholderColor.Z);

	MeshComponent->SetMobility(Mobility);

	if (ACharacter* Character = Cast<ACharacter>(&Actor))
	{
		// SWG geometry is authored with its origin at ground level, but
		// ACharacter's root is the capsule, centered above the feet — this
		// offset keeps the mesh from floating at capsule-center height. The
		// capsule is also resized here to the real mesh bounds (not UE's
		// flat 88/34 default), since every capsule-derived offset (this one,
		// camera eye height, nameplate clearance) is wrong otherwise.
		if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			const FBox MeshBounds = StaticMesh->GetBoundingBox();
			MeshComponent->RegisterComponent();
			MeshComponent->AttachToComponent(Capsule, FAttachmentTransformRules::KeepRelativeTransform);
			MeshComponent->SetRelativeRotation(FRotator(0.0f, YawCorrectionDegrees, 0.0f));

			if (MeshBounds.IsValid)
			{
				// Read the server's real feet-level Z from ASWGCreature::LastNetworkZ
				// rather than back-calculating "CurrentLocation.Z - OldHalfHeight" —
				// the async mesh queue can take seconds, during which an
				// unconstrained freefall (before terrain collision exists) can
				// move the actor and corrupt that back-calculation.
				const ASWGCreature* Creature = Cast<ASWGCreature>(&Actor);
				const float NetworkZ = Creature ? Creature->LastNetworkZ : (Actor.GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight());

				const FVector Extent = MeshBounds.GetExtent();
				const float NewHalfHeight = FMath::Max(Extent.Z, 1.0f);
				const float NewRadius = FMath::Max(FMath::Max(Extent.X, Extent.Y), 1.0f);
				Capsule->SetCapsuleSize(NewRadius, NewHalfHeight);

				FVector CorrectedLocation = Actor.GetActorLocation();
				CorrectedLocation.Z = NetworkZ + NewHalfHeight;
				Actor.SetActorLocation(CorrectedLocation);

				MeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -MeshBounds.GetCenter().Z));

				// The player's first-person eye height is derived from the
				// capsule at construction time (before this resize), so it
				// needs recomputing now against the real size.
				if (ASWGPlayer* Player = Cast<ASWGPlayer>(&Actor))
				{
					Player->UpdateCameraHeight();
				}

				// Same problem for the nameplate — UpdateNameLabel may have
				// already run (e.g. from an earlier baseline packet) and
				// positioned it against the capsule's pre-resize default.
				if (USWGTangibleComponent* Tangible = Actor.FindComponentByClass<USWGTangibleComponent>())
				{
					Tangible->RepositionNameLabel();
				}
			}
			else
			{
				MeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -Capsule->GetScaledCapsuleHalfHeight()));
			}
		}

		// ACharacter always carries its own default USkeletalMeshComponent with no
		// SkeletalMesh assigned; we never feed decoded geometry into it (that needs
		// the same editor-only import pipeline this generator avoids for the
		// procedural path), so just hide it. The capsule-half-height offset
		// (not this component's transform) is what fixes NPCs floating.
		if (USkeletalMeshComponent* CharacterMesh = Character->GetMesh())
		{
			CharacterMesh->SetVisibility(false);
			CharacterMesh->SetHiddenInGame(true);
		}
	}
	else
	{
		// Non-Character (items, world-snapshot static objects): replace
		// whatever default root Actor has outright, rather than attaching
		// under it. A Static component in particular can't attach under a
		// Movable default root, and its transform can't be changed via
		// SetWorldTransform once registered ("has to be Movable if you'd
		// like to move") — and there's no capsule-derived reason to keep a
		// separate root around for either mobility. Compose the yaw
		// correction on top of the old root's world transform (the actor's
		// already-correct network spawn placement), since nothing else
		// carries that placement once the old root is gone.
		const FTransform BaseTransform = Actor.GetRootComponent() ? Actor.GetRootComponent()->GetComponentTransform() : Actor.GetActorTransform();
		const FTransform YawCorrectionTransform(FRotator(0.0f, YawCorrectionDegrees, 0.0f));
		MeshComponent->SetRelativeTransform(YawCorrectionTransform * BaseTransform);
		Actor.SetRootComponent(MeshComponent);
		MeshComponent->RegisterComponent();
	}

	OnMeshReady.Broadcast(&Actor, MeshComponent);

	return MeshComponent;
}

namespace
{
	// "appearance/mesh/wke_m_head_l0.mgn" -> "appearance/skeleton/wke_m_face.skt".
	// Simply skipped if the file doesn't exist for a given species.
	FString DeriveFaceSkeletonPath(const TArray<FString>& MeshVirtualPaths)
	{
		for (const FString& MeshPath : MeshVirtualPaths)
		{
			const FString BaseName = FPaths::GetBaseFilename(MeshPath);
			const int32 HeadIndex = BaseName.Find(TEXT("_head"), ESearchCase::IgnoreCase);
			if (HeadIndex == INDEX_NONE)
			{
				continue;
			}
			return FString::Printf(TEXT("appearance/skeleton/%s_face.skt"), *BaseName.Left(HeadIndex));
		}
		return FString();
	}
}

const TMap<FString, FString>& USWGMeshGeneratorSubsystem::GetSlotHardpoints()
{
	if (bSlotHardpointsLoaded)
	{
		return SlotHardpoints;
	}
	bSlotHardpointsLoaded = true;

	TArray<FSWGSlotDefinition> Definitions;
	if (!FSWGSlotDefinitionReader::Read(
		TreSubsystem->CreateIffReader(TEXT("abstract/slot/slot_definition/slot_definitions.iff")),
		Definitions))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to read slot definitions"));
		return SlotHardpoints;
	}

	for (const FSWGSlotDefinition& Definition : Definitions)
	{
		if (!Definition.HardpointName.IsEmpty())
		{
			SlotHardpoints.Add(Definition.Name, Definition.HardpointName);
		}
	}
	return SlotHardpoints;
}

USkeletalMesh* USWGMeshGeneratorSubsystem::GetOrBuildGeneratedSkeletalMesh(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FSWGSkeletonData& Skeleton)
{
	const uint32 PathsHash = GetTypeHash(SkeletonPath) ^ GetTypeHash(MeshVirtualPaths);
	const FString AssetName = FString::Printf(TEXT("SK_%u"), PathsHash);
	const FString PackagePath = TEXT("/Game/SWGEmu/Generated/") + AssetName;

	// The saved .uasset on disk is the cache — a hit here skips rebuilding
	// entirely, same role GetOrBuildGeneratedStaticMesh's own cache plays.
	if (USkeletalMesh* Existing = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName)))
	{
		return Existing;
	}

#if WITH_EDITOR
	FSWGSkeletonData MeshBuildSkeleton = Skeleton;
	const FString FaceSkeletonPath = DeriveFaceSkeletonPath(MeshVirtualPaths);
	if (!FaceSkeletonPath.IsEmpty())
	{
		FSWGSkeletonData FaceSkeleton;
		if (FSWGSkeletonReader::ReadSkeleton(TreSubsystem->CreateIffReader(FaceSkeletonPath), FaceSkeleton))
		{
			const int32 HeadJointIndex = MeshBuildSkeleton.Joints.IndexOfByPredicate([](const FSWGSkeletonJoint& Joint) { return Joint.Name.Equals(TEXT("head"), ESearchCase::IgnoreCase); });
			if (HeadJointIndex != INDEX_NONE)
			{
				const int32 FaceBaseIndex = MeshBuildSkeleton.Joints.Num();
				for (FSWGSkeletonJoint Joint : FaceSkeleton.Joints)
				{
					Joint.ParentIndex = Joint.ParentIndex == INDEX_NONE ? HeadJointIndex : FaceBaseIndex + Joint.ParentIndex;
					MeshBuildSkeleton.Joints.Add(MoveTemp(Joint));
				}
			}
		}
	}

	TArray<FSWGMeshData> MeshParts;
	MeshParts.Reserve(MeshVirtualPaths.Num());
	for (const FString& MeshPath : MeshVirtualPaths)
	{
		FSWGMeshData PartData;
		if (FSWGMeshReader::ReadSkeletalMeshBindPose(TreSubsystem->CreateIffReader(MeshPath), PartData))
		{
			MeshParts.Add(MoveTemp(PartData));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to parse skeletal mesh part '%s' while building generated mesh for skeleton '%s'"), *MeshPath, *SkeletonPath);
		}
	}
	if (MeshParts.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: no usable mesh parts to build a generated skeletal mesh for skeleton '%s'"), *SkeletonPath);
		return nullptr;
	}

	TArray<const FSWGMeshData*> MeshPartPtrs;
	MeshPartPtrs.Reserve(MeshParts.Num());
	for (const FSWGMeshData& Part : MeshParts)
	{
		MeshPartPtrs.Add(&Part);
	}

	USkeletalMesh* Result = FSWGSkeletalMeshImporter::BuildSkeletalMesh(
		MeshBuildSkeleton,
		MeshPartPtrs,
		PackagePath,
		GetSlotHardpoints());
	if (!Result)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to build generated skeletal mesh '%s' for skeleton '%s'"), *PackagePath, *SkeletonPath);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: built and cached generated skeletal mesh '%s' for skeleton '%s'"), *PackagePath, *SkeletonPath);
	}
	return Result;
#else
	UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: generated skeletal mesh '%s' isn't built yet and can't be built in a packaged build — leaving skeleton '%s' on its procedural bind-pose mesh"), *PackagePath, *SkeletonPath);
	return nullptr;
#endif
}

UAnimSequence* USWGMeshGeneratorSubsystem::GetOrBuildLocomotionAnimSequence(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FString& ClipPath, const FSWGSkeletonData& Skeleton, USkeleton* TargetSkeleton)
{
	const uint32 PathsHash = GetTypeHash(SkeletonPath) ^ GetTypeHash(MeshVirtualPaths) ^ GetTypeHash(ClipPath);
	const FString AssetName = FString::Printf(TEXT("AS_%u"), PathsHash);
	const FString PackagePath = TEXT("/Game/SWGEmu/Generated/") + AssetName;

	if (UAnimSequence* Existing = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName)))
	{
		return Existing;
	}

#if WITH_EDITOR
	FSWGAnimationData ClipAnimation;
	if (!FSWGAnimationReader::ReadAnimation(TreSubsystem->CreateIffReader(ClipPath), ClipAnimation))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to decode locomotion clip '%s' while building '%s'"), *ClipPath, *PackagePath);
		return nullptr;
	}

	UAnimSequence* Result = FSWGAnimationImporter::BuildAnimSequence(ClipAnimation, Skeleton, TargetSkeleton, PackagePath);
	if (!Result)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to build generated anim sequence '%s' from '%s'"), *PackagePath, *ClipPath);
	}
	return Result;
#else
	UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: generated anim sequence '%s' isn't built yet and can't be built in a packaged build"), *PackagePath);
	return nullptr;
#endif
}

UBlendSpace* USWGMeshGeneratorSubsystem::GetOrBuildLocomotionBlendSpace(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const TArray<FString>& LocomotionPaths, UAnimSequence* IdleSequence, UAnimSequence* WalkSequence, UAnimSequence* RunSequence, float WalkSpeed, float RunSpeed, USkeleton* TargetSkeleton)
{
	const uint32 PathsHash = GetTypeHash(SkeletonPath) ^ GetTypeHash(MeshVirtualPaths) ^ GetTypeHash(LocomotionPaths);
	const FString AssetName = FString::Printf(TEXT("BS_%u"), PathsHash);
	const FString PackagePath = TEXT("/Game/SWGEmu/Generated/") + AssetName;

	if (UBlendSpace* Existing = LoadObject<UBlendSpace>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName)))
	{
		return Existing;
	}

#if WITH_EDITOR
	if (!IdleSequence || !WalkSequence || !RunSequence)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: missing a locomotion clip — can't build blend space '%s'"), *PackagePath);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();
	// UBlendSpace1D rather than the generic 3-axis UBlendSpace: all our
	// samples vary only along X (speed), and this subclass exposes that
	// intent directly — a plain bScaleAnimation bool instead of the base
	// class's protected AxisToScaleAnimation enum.
	UBlendSpace1D* Result = NewObject<UBlendSpace1D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	Result->SetSkeleton(TargetSkeleton);

	// Scale each sample's playback rate by how far the live blend input is
	// from that sample's own speed — same role the old ReferenceSpeed rate
	// clamp played, now owned by the engine instead of Tick().
	Result->bScaleAnimation = true;

	// BlendParameters is EditAnywhere but C++-protected (editor tooling
	// normally sets it through the Details panel, which goes through
	// FProperty reflection rather than direct member access) —
	// ContainerPtrToValuePtr is the same mechanism, just called from code.
	if (FStructProperty* BlendParametersProp = FindFProperty<FStructProperty>(UBlendSpace::StaticClass(), TEXT("BlendParameters")))
	{
		FBlendParameter* BlendParameters = BlendParametersProp->ContainerPtrToValuePtr<FBlendParameter>(Result);
		BlendParameters[0].DisplayName = TEXT("Speed");
		BlendParameters[0].Min = 0.0f;
		BlendParameters[0].Max = RunSpeed;
		BlendParameters[0].GridNum = 4;
	}

	Result->AddSample(IdleSequence, FVector(0.0f, 0.0f, 0.0f));
	Result->AddSample(WalkSequence, FVector(WalkSpeed, 0.0f, 0.0f));
	Result->AddSample(RunSequence, FVector(RunSpeed, 0.0f, 0.0f));

	// ResampleData(), not just ValidateSampleData(): adding samples only fills
	// SampleData (the authored sample list). The *runtime* structure the
	// evaluator actually reads — BlendSpaceData, the line segments derived
	// from those samples — is built solely by ResampleData (which validates
	// internally, then derives DimensionIndices from the sample AABB and runs
	// the 1D segmentation). Without it BlendSpaceData stays empty, so
	// GetSamplesFromBlendInput returns nothing at runtime and the evaluated
	// pose falls through as identity local rotations — which renders as a
	// T-pose for this skeleton, whose arms only come down once the composed
	// bind rotation is applied. Nothing warns: the asset itself still has
	// valid samples, a valid skeleton, and reports IsPlaying.
	Result->ResampleData();

	Result->MarkPackageDirty();
	Result->PostEditChange();
	FAssetRegistryModule::AssetCreated(Result);

	const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	UPackage::SavePackage(Package, Result, *FileName, SaveArgs);

	UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: built and cached generated blend space '%s' for skeleton '%s'"), *PackagePath, *SkeletonPath);
	return Result;
#else
	UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: generated blend space '%s' isn't built yet and can't be built in a packaged build"), *PackagePath);
	return nullptr;
#endif
}

bool USWGMeshGeneratorSubsystem::TryApplyGeneratedAnimatedMesh(AActor& Actor, const TArray<FString>& MeshVirtualPaths, const TMap<FString, FString>& AnimationLatPaths, UMeshComponent* ProceduralMeshComponent, const TMap<FString, FLinearColor>* PaletteTintOverrides, const TMap<FString, float>* MorphWeights, const TMap<FString, int32>* TextureIndexOverrides)
{
	if (!TreSubsystem)
	{
		return false;
	}

	ACharacter* Character = Cast<ACharacter>(&Actor);
	USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
	if (!CharacterMesh)
	{
		return false;
	}

	FString SkeletonPath;
	for (const FString& MeshPath : MeshVirtualPaths)
	{
		SkeletonPath = FSWGMeshReader::ReadSkeletalMeshSkeletonPath(TreSubsystem->CreateIffReader(MeshPath));
		if (!SkeletonPath.IsEmpty()) break;
	}
	if (SkeletonPath.IsEmpty())
	{
		// No SKTM skeleton reference — this object has no animation data at
		// all (most world objects), not an error; leave the procedural
		// bind-pose mesh in place.
		return false;
	}

	const FString* LatPath = AnimationLatPaths.Find(SkeletonPath);
	TArray<FString> LocomotionPaths;
	if (!LatPath || !ReadDefaultLocomotionPaths(TreSubsystem->CreateIffReader(*LatPath), LocomotionPaths))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: no usable LATX locomotion LAT for skeleton '%s'"), *SkeletonPath);
		return false;
	}

	FSWGSkeletonData Skeleton;
	if (!FSWGSkeletonReader::ReadSkeleton(TreSubsystem->CreateIffReader(SkeletonPath), Skeleton))
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to parse SKTM skeleton '%s'"), *SkeletonPath);
		return false;
	}

	USkeletalMesh* GeneratedMesh = GetOrBuildGeneratedSkeletalMesh(SkeletonPath, MeshVirtualPaths, Skeleton);
	if (!GeneratedMesh)
	{
		return false;
	}

	// TargetSkeleton is the actual asset skeleton GeneratedMesh was built
	// against (may include merged face bones — see GetOrBuildGeneratedSkeletalMesh),
	// which is what a UAnimSequence/UBlendSpace played on this component must
	// be compatible with; Skeleton (the plain joint list) is what the
	// animated tracks themselves get built from.
	USkeleton* TargetSkeleton = GeneratedMesh->GetSkeleton();
	UAnimSequence* IdleSequence = GetOrBuildLocomotionAnimSequence(SkeletonPath, MeshVirtualPaths, LocomotionPaths[0], Skeleton, TargetSkeleton);
	UAnimSequence* WalkSequence = GetOrBuildLocomotionAnimSequence(SkeletonPath, MeshVirtualPaths, LocomotionPaths[1], Skeleton, TargetSkeleton);
	UAnimSequence* RunSequence = GetOrBuildLocomotionAnimSequence(SkeletonPath, MeshVirtualPaths, LocomotionPaths[3], Skeleton, TargetSkeleton);

	const USWGMovementComponent* Movement = Cast<USWGMovementComponent>(Character->GetCharacterMovement());
	const float WalkSpeed = Movement && Movement->WalkSpeed > KINDA_SMALL_NUMBER
		? Movement->WalkSpeed * 100.0f
		: 155.0f;
	const float RunSpeed = Movement && Movement->RunSpeed > Movement->WalkSpeed
		? Movement->RunSpeed * 100.0f
		: FMath::Max(WalkSpeed * 2.0f, Movement ? Movement->MaxWalkSpeed : 310.0f);

	UBlendSpace* LocomotionBlendSpace = GetOrBuildLocomotionBlendSpace(SkeletonPath, MeshVirtualPaths, LocomotionPaths, IdleSequence, WalkSequence, RunSequence, WalkSpeed, RunSpeed, TargetSkeleton);
	if (!LocomotionBlendSpace)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: %s has no usable locomotion blend space; leaving the procedural mesh visible instead of showing a T-pose"), *Actor.GetName());
		return false;
	}

	CharacterMesh->SetSkeletalMesh(GeneratedMesh);
	CharacterMesh->SetVisibility(true);
	CharacterMesh->SetHiddenInGame(false);

	// SWG geometry (both the procedural generated-mesh path and this skeletal
	// mesh, which shares the same .mgn source data/authoring convention) is
	// authored feet-at-origin, but ACharacter's capsule is centered on the
	// actor — same fallback BuildGeneratedMeshComponent applies to the
	// procedural mesh when it has no bounds to compute a precise
	// center-based offset.
	if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		CharacterMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -Capsule->GetScaledCapsuleHalfHeight()));
	}

	// SWG's forward axis is 90 degrees off from Unreal's — the same quirk
	// ASWGPlayer's camera/control rotation already corrects for. That fix
	// doesn't touch the mesh itself, so rotate the whole mesh rigidly here.
	CharacterMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

	// The importer creates material slots named after each submesh's shader
	// path (see FSWGSkeletalMeshImporter::BuildSkeletalMesh) but leaves the
	// actual material null — build/assign the same real per-shader textured
	// materials the procedural generated-mesh path already uses.
	int32 NumSkeletalTextured = 0;
	for (int32 SlotIndex = 0; SlotIndex < GeneratedMesh->GetMaterials().Num(); ++SlotIndex)
	{
		const FString ShaderPath = GeneratedMesh->GetMaterials()[SlotIndex].MaterialSlotName.ToString();
		if (UMaterialInterface* Material = GetOrBuildObjectMaterial(ShaderPath, PaletteTintOverrides, TextureIndexOverrides))
		{
			CharacterMesh->SetMaterial(SlotIndex, Material);
			++NumSkeletalTextured;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: %s skeletal mesh slot %d ('%s') has no real material — leaving the mesh's default"),
				*Actor.GetName(), SlotIndex, *ShaderPath);
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: %s skeletal mesh assigned %d/%d real textured material(s)"),
		*Actor.GetName(), NumSkeletalTextured, GeneratedMesh->GetMaterials().Num());

	// Real per-character face/body shape (see ResolveCustomizationMorphWeights)
	// — a no-op for any name GeneratedMesh doesn't actually have a matching
	// morph target for (SetMorphTarget just logs nothing and does nothing).
	if (MorphWeights)
	{
		for (const TPair<FString, float>& Pair : *MorphWeights)
		{
			CharacterMesh->SetMorphTarget(FName(*Pair.Key), Pair.Value);
		}
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: applied %d morph weight(s) to '%s' on mesh '%s'"),
			MorphWeights->Num(), *Actor.GetName(), *GetNameSafe(CharacterMesh->GetSkeletalMeshAsset()));
	}

	CharacterMesh->PlayAnimation(LocomotionBlendSpace, true);
	UAnimSingleNodeInstance* AnimInstance = Cast<UAnimSingleNodeInstance>(CharacterMesh->GetAnimInstance());
	if (!AnimInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: %s failed to start blend space playback; leaving the procedural mesh visible instead of showing a T-pose"), *Actor.GetName());
		CharacterMesh->SetVisibility(false);
		CharacterMesh->SetHiddenInGame(true);
		if (ProceduralMeshComponent)
		{
			ProceduralMeshComponent->SetVisibility(true);
			ProceduralMeshComponent->SetHiddenInGame(false);
		}
		return false;
	}

	FSWGPlayingAnimation Playing;
	Playing.MeshComponent = CharacterMesh;
	Playing.AnimInstance = AnimInstance;
	PlayingAnimations.Add(MoveTemp(Playing));

	// A player can receive more than one asynchronous mesh request (and a
	// previous PIE run can leave an actor-owned procedural component around).
	// Hiding only the component from *this* request left an older bind-pose
	// mesh rendering through the generated skeletal mesh, which looked like a
	// second, incorrectly UV-mapped face at the Wookiee's waist. The
	// procedural fallback is a UStaticMeshComponent now (see
	// BuildGeneratedMeshComponent), not a UDynamicMeshComponent.
	TInlineComponentArray<UStaticMeshComponent*> ProceduralMeshComponents(&Actor);
	for (UStaticMeshComponent* ProceduralMesh : ProceduralMeshComponents)
	{
		if (ProceduralMesh)
		{
			ProceduralMesh->SetVisibility(false);
			ProceduralMesh->SetHiddenInGame(true);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: %s resolved to the generated skeletal mesh for skeleton '%s'"), *Actor.GetName(), *SkeletonPath);
	return true;
}
