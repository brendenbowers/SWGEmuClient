#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TRE/SWGIffReader.h"
#include "TRE/SWGDDSTextureLoader.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Misc/Optional.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGStaticProp.h"
#include "Objects/World/SWGInstallation.h"
#include "Objects/Tangible/SWGItem.h"
#include "Objects/Player/SWGPlayer.h"
#include "Components/SWGTangibleComponent.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeExit.h"

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

	bool FindChildForm(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& FormType, FSWGIffChunk& OutChunk)
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

	/** All FORM children (any FormType) among Parent's direct children — used where a version-tagged wrapper form's exact tag isn't consistent (see ResolveShaderDiffuseTexturePath). */
	TArray<FSWGIffChunk> FindChildForms(const FSWGIffReader& Reader, const FSWGIffChunk& Parent)
	{
		TArray<FSWGIffChunk> Result;
		for (const FSWGIffChunk& Child : Reader.ReadChildren(Parent))
		{
			if (Child.IsForm())
			{
				Result.Add(Child);
			}
		}
		return Result;
	}

	bool FindChildChunk(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, const FString& Tag, FSWGIffChunk& OutChunk)
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

	// Finds the one non-DERV FORM child of a SCOT/STOT/SHOT node — the
	// versioned data form (0007/0008/0009/...) holding that layer's XXXX
	// key-value fields (DERV is the base-template reference, a sibling, not
	// the data itself).
	bool FindVersionedDataForm(const FSWGIffReader& Reader, const FSWGIffChunk& Parent, FSWGIffChunk& OutForm)
	{
		for (const FSWGIffChunk& Child : Reader.ReadChildren(Parent))
		{
			if (Child.IsForm() && Child.FormType != TEXT("DERV"))
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
			if (Child.Tag != TEXT("XXXX"))
			{
				continue;
			}

			const uint8* Data = Reader.GetChunkData(Child);
			const int32 Size = Reader.GetChunkSize(Child);

			int32 KeyEnd = 0;
			while (KeyEnd < Size && Data[KeyEnd] != 0) { ++KeyEnd; }

			if (FString::ConstructFromPtrSize((const ANSICHAR*)Data, KeyEnd) != Key)
			{
				continue;
			}

			const int32 FlagOffset = KeyEnd + 1;
			if (FlagOffset >= Size || Data[FlagOffset] == 0)
			{
				return false; // no value set
			}

			const int32 ValueOffset = FlagOffset + 1;
			int32 ValueEnd = ValueOffset;
			while (ValueEnd < Size && Data[ValueEnd] != 0) { ++ValueEnd; }

			OutValue = FString::ConstructFromPtrSize((const ANSICHAR*)(Data + ValueOffset), ValueEnd - ValueOffset);
			return !OutValue.IsEmpty();
		}

		return false;
	}

	bool FindAppearanceFilename(const FSWGIffReader& Reader, const FSWGIffChunk& DataForm, FString& OutAppearancePath)
	{
		return FindXxxxStringValue(Reader, DataForm, TEXT("appearanceFilename"), OutAppearancePath);
	}

	// Buildings with interiors (spaceports, cantinas, etc.) leave
	// appearanceFilename empty entirely — not inherited via DERV either — and
	// instead reference their exterior shell through portalLayoutFilename
	// (.pob). Per Core3's PortalLayout::parse/parseCELSForm and CellProperty
	// (see chat history), the .pob's CELS form holds one FORM CELL per
	// interior room PLUS one implicit "cell 0" representing the outside —
	// getCellTotalNumber() explicitly excludes it ("cellProperties.size() - 1").
	// Cell 0's own meshFile field (read positionally, not as an XXXX
	// key/value — see CellProperty::loadVersion4/5) is the building's outer
	// shell appearance path. We only need that one field; interior
	// cells/floor meshes/portals are out of scope for now.
	bool ReadPobExteriorMeshPath(const FSWGIffReader& Reader, FString& OutAppearancePath)
	{
		FSWGIffChunk PrtoForm;
		if (!Reader.FindForm(TEXT("PRTO"), PrtoForm))
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
			if (Child.IsForm() && Child.FormType == TEXT("CELS"))
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
		if (CellForms.Num() == 0 || !CellForms[0].IsForm() || CellForms[0].FormType != TEXT("CELL"))
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
			if (Child.Tag == TEXT("DATA"))
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
	// the key/flag/value layout parseVariableData uses — this is Core3's
	// loadDerv() reading a plain Chunk::readString()). Buildings' own .iff
	// almost always has an empty appearanceFilename because it's inherited
	// from this base template (e.g. object/building/base/shared_*.iff) rather
	// than redefined per-building — this is why every building template was
	// failing the appearanceFilename lookup.
	bool FindDervParentPath(const FSWGIffReader& Reader, const FSWGIffChunk& ShotForm, FString& OutParentPath)
	{
		for (const FSWGIffChunk& Child : Reader.ReadChildren(ShotForm))
		{
			if (Child.IsForm() && Child.FormType == TEXT("DERV"))
			{
				for (const FSWGIffChunk& DervChild : Reader.ReadChildren(Child))
				{
					if (DervChild.Tag == TEXT("XXXX"))
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
			if (Child.Tag != TEXT("XXXX"))
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
}

void USWGMeshGeneratorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	TreSubsystem = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));
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

	FSWGPendingMeshRequest Request = PendingRequests.Pop();
	// Temporary diagnostic: pin down exactly where a request silently vanishes
	// without hitting any existing error/warning log (confirmed live for CRC
	// 4E38DA33 — two creatures sharing it never got a mesh at all, even after
	// the pending-request queue fully drained).
	UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: ProcessNextRequest starting for actor %s (crc %08X, %d path(s) already resolved)"),
		Request.Actor.IsValid() ? *Request.Actor->GetName() : TEXT("<gone>"), Request.TemplateCrc, Request.MeshVirtualPaths.Num());
	Async(EAsyncExecution::Thread, [this, Request = MoveTemp(Request)]() mutable
		{

			if (Request.TemplateCrc != 0 && Request.MeshVirtualPaths.IsEmpty())
			{
				if (!ResolveMeshPath(Request.TemplateCrc, Request.MeshVirtualPaths, Request.bSkeletal))
				{
					UE_LOG(LogTemp, Error, TEXT("USWGMeshGeneratorSubsystem: failed to resolve mesh path for template CRC %08X"), Request.TemplateCrc);
					return;
				}
			}
			else if (!Request.TemplatePath.IsEmpty() && Request.MeshVirtualPaths.IsEmpty())
			{
				if (!ResolveMeshPathForTemplate(Request.TemplatePath, Request.MeshVirtualPaths, Request.bSkeletal))
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

			// Cache key includes the actor's class alongside the resolved mesh
			// paths: PlaceholderColor (baked directly into the cached mesh's
			// vertex colors — see BuildDynamicMesh) is a pure function of
			// actor class, so two different classes sharing the same
			// underlying mesh file(s) must not share one cache entry, or
			// whichever built it first "poisons" the tint for the other.
			const FString ActorClassName = Request.Actor.IsValid() ? Request.Actor->GetClass()->GetName() : TEXT("Unknown");
			const uint32 PathsHash = GetTypeHash(Request.MeshVirtualPaths);
			const FString MeshCacheDir = FPaths::ProjectSavedDir() / TEXT("MeshCache");
			const FString MeshCachePath = FString::Printf(TEXT("%s/%u_%s.dmesh"), *MeshCacheDir, PathsHash, *ActorClassName);

			// Two actors sharing the same template (very common — many
			// instances of one creature CRC, or several spawned in the same
			// burst) can request this exact cache path at nearly the same
			// moment. Deciding read-vs-write-vs-skip has to happen atomically
			// under one lock, or two requests could both see "file doesn't
			// exist yet" and both start writing it (or one starts reading a
			// file the other is still mid-write on) — confirmed live as the
			// cause of a creature silently never getting a mesh at all (no
			// error logged, capsule stuck at the un-resized default forever).
			bool bTryCacheRead = false;
			bool bShouldWriteCache = false;
			{
				FScopeLock Lock(&CacheWriteLock);
				if (InFlightCacheWrites.Contains(MeshCachePath))
				{
					// Someone else is already building+writing this exact
					// entry — don't race them. Just parse and build fresh for
					// this request, without reading or writing the cache.
				}
				else if (IFileManager::Get().FileExists(*MeshCachePath))
				{
					bTryCacheRead = true;
				}
				else
				{
					InFlightCacheWrites.Add(MeshCachePath);
					bShouldWriteCache = true;
				}
			}

			if (bTryCacheRead)
			{
				// UObject creation (NewObject) and anything touching the actor
				// itself has to happen on the game thread — only the
				// existence check above is safe to do here. See the
				// fresh-parse branch below for the same actor-gone-mid-async
				// guard this mirrors.
				AsyncTask(ENamedThreads::GameThread, [this, Request, MeshCachePath]() mutable
					{
						if (!Request.Actor.IsValid())
						{
							UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: actor gone before cached mesh could be loaded for %s — skipping"), *FString::Join(Request.MeshVirtualPaths, TEXT(", ")));
							return;
						}

						FArchive* FileReader = IFileManager::Get().CreateFileReader(*MeshCachePath);
						if (!FileReader)
						{
							UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: cache file %s vanished before it could be read — skipping this mesh"), *MeshCachePath);
							return;
						}

						// UDynamicMesh::Serialize (called on the UObject itself) also
						// runs Super::Serialize (UObject's tagged-property/versioning
						// path), which expects a proper linker/package archive context
						// — round-tripping it through a bare IFileManager archive
						// produced multiple creatures stuck with an un-resized default
						// capsule (confirmed live: capsule never resized after caching
						// was introduced, exactly for repeat/cached spawns of the same
						// mesh). FDynamicMesh3::Serialize (via GetMeshRef()) is a plain
						// data serializer with no UObject involvement, safe for a bare
						// archive.
						UDynamicMesh* DynamicMesh = NewObject<UDynamicMesh>(this);
						DynamicMesh->GetMeshRef().Serialize(*FileReader);

						// Per-submesh shader names, persisted right after the mesh
						// data itself (see the write side below) — needed to build
						// real per-shader textured materials on a cache hit, not
						// just the geometry. Cache files from before this was added
						// simply fail here (harmless: dev-only on-disk cache,
						// delete Saved/MeshCache to regenerate in the new format).
						TArray<FString> ShaderNames;
						*FileReader << ShaderNames;

						const bool bReadOk = !FileReader->IsError();
						FileReader->Close();
						delete FileReader;

						if (!bReadOk)
						{
							UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: corrupt mesh cache %s — deleting and skipping this mesh"), *MeshCachePath);
							IFileManager::Get().Delete(*MeshCachePath);
							return;
						}

						UE_LOG(LogTemp, Log, TEXT("USWGMeshGeneratorSubsystem: loaded cached mesh for %s from %s"), *Request.Actor->GetName(), *MeshCachePath);
						BuildDynamicMesh(*Request.Actor, DynamicMesh, ShaderNames);
					});
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: parsing fresh for actor %s (crc %08X)"),
					Request.Actor.IsValid() ? *Request.Actor->GetName() : TEXT("<gone>"), Request.TemplateCrc);

				FSWGMeshData MeshData;
				const bool bParsed = ParseMesh(Request, MeshData);

				UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: ParseMesh returned %s for actor %s (crc %08X), %d submesh(es)"),
					bParsed ? TEXT("true") : TEXT("false"), Request.Actor.IsValid() ? *Request.Actor->GetName() : TEXT("<gone>"), Request.TemplateCrc, MeshData.Submeshes.Num());

				// Temporary diagnostic: investigating real object texturing —
				// need to see what ShaderName actually looks like on disk
				// (bare name vs. full virtual path, .sht extension or not)
				// before a shader->texture resolver can be written.
				for (const FSWGMeshSubmesh& Submesh : MeshData.Submeshes)
				{
					UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: submesh ShaderName='%s'"), *Submesh.ShaderName);
				}

				if (bParsed)
				{
					// Back on the game thread to build the mesh component and attach it to the actor.
					// The actor can be destroyed/GC'd during this background parse (streaming out,
					// zone change, etc.) — Request.Actor is a TWeakObjectPtr specifically for that
					// reason, but nothing here checked it before dereferencing, which crashed
					// NewObject (null Outer) once an actor happened to go away mid-parse.
					AsyncTask(ENamedThreads::GameThread, [this, Request, MeshData, MeshCacheDir, MeshCachePath, bShouldWriteCache]()
						{
							// Whichever branch this lambda exits through, the
							// in-flight marker (if we set one) must come off,
							// or every later request for this same mesh would
							// be permanently treated as "someone else is
							// writing it" and never actually cache/build.
							ON_SCOPE_EXIT
							{
								if (bShouldWriteCache)
								{
									FScopeLock Lock(&CacheWriteLock);
									InFlightCacheWrites.Remove(MeshCachePath);
								}
							};

							if (!Request.Actor.IsValid())
							{
								UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: actor gone before mesh build completed for %s — skipping"), *FString::Join(Request.MeshVirtualPaths, TEXT(", ")));
								return;
							}

							UMeshComponent* MeshComponent = BuildDynamicMesh(*Request.Actor, MeshData);
							UDynamicMeshComponent* DynamicMeshComponent = Cast<UDynamicMeshComponent>(MeshComponent);
							if (!DynamicMeshComponent || !bShouldWriteCache)
							{
								return;
							}

							IFileManager::Get().MakeDirectory(*MeshCacheDir, true);
							if (FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*MeshCachePath))
							{
								// See the cache-read branch's comment — plain
								// FDynamicMesh3::Serialize, not UDynamicMesh::Serialize.
								DynamicMeshComponent->GetDynamicMesh()->GetMeshRef().Serialize(*FileWriter);

								TArray<FString> ShaderNames;
								ShaderNames.Reserve(MeshData.Submeshes.Num());
								for (const FSWGMeshSubmesh& Submesh : MeshData.Submeshes)
								{
									ShaderNames.Add(Submesh.ShaderName);
								}
								*FileWriter << ShaderNames;

								FileWriter->Close();
								delete FileWriter;
							}
							else
							{
								UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to open %s for writing — mesh not cached this time"), *MeshCachePath);
							}
						});
				}
				else if (bShouldWriteCache)
				{
					// ParseMesh itself failed — still have to release the
					// marker so a later attempt for this same mesh isn't
					// permanently blocked from ever using the cache.
					FScopeLock Lock(&CacheWriteLock);
					InFlightCacheWrites.Remove(MeshCachePath);
				}
			}
		});
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

bool USWGMeshGeneratorSubsystem::ResolveMeshPath(uint32 TemplateCrc, TArray<FString>& OutMeshVirtualPaths, bool& bOutSkeletal)
{
	const FString TemplatePath = TreSubsystem->ResolveTemplatePath(TemplateCrc);
	if (TemplatePath.IsEmpty())
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: no template path for CRC %08X"), TemplateCrc);
		return false;
	}

	return ResolveMeshPathForTemplate(TemplatePath, OutMeshVirtualPaths, bOutSkeletal);
}

bool USWGMeshGeneratorSubsystem::ResolveMeshPathForTemplate(const FString& TemplatePath, TArray<FString>& OutMeshVirtualPaths, bool& bOutSkeletal)
{
	// Chain confirmed by direct inspection of real TRE files (see chat history,
	// not just the design doc's summary):
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
	if (!TemplateReader.FindForm(TEXT("SHOT"), ShotForm) || !FindVersionedDataForm(TemplateReader, ShotForm, ShotDataForm))
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
				|| !ParentReader.FindForm(TEXT("SHOT"), ParentShotForm)
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
		FSWGIffChunk SmatForm, F0003, MsgnChunk;
		if (!AppearanceReader.FindForm(TEXT("SMAT"), SmatForm)
			|| !FindChildForm(AppearanceReader, SmatForm, TEXT("0003"), F0003)
			|| !FindChildChunk(AppearanceReader, F0003, TEXT("MSGN"), MsgnChunk))
		{
			UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: %s missing SMAT/0003/MSGN structure"), *AppearancePath);
			return false;
		}

		// One or more null-terminated body-part .lmg references packed into
		// this single chunk (see SplitNullTerminatedStrings comment).
		MeshGroupPaths = SplitNullTerminatedStrings(AppearanceReader, MsgnChunk);
		bOutSkeletal = true;
	}
	else
	{
		FSWGIffChunk AptForm, Form0000, NameChunk;
		if (!AppearanceReader.FindForm(TEXT("APT "), AptForm)
			|| !FindChildForm(AppearanceReader, AptForm, TEXT("0000"), Form0000)
			|| !FindChildChunk(AppearanceReader, Form0000, TEXT("NAME"), NameChunk))
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
			if (!GroupReader.FindForm(TEXT("MLOD"), MlodForm) || !FindChildForm(GroupReader, MlodForm, TEXT("0000"), Form0000))
			{
				UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: %s missing MLOD/0000 structure"), *MeshGroupPath);
				continue;
			}
			for (const FSWGIffChunk& Child : GroupReader.ReadChildren(Form0000))
			{
				if (Child.Tag == TEXT("NAME"))
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
			if (!GroupReader.FindForm(TEXT("DTLA"), DtlaForm)
				|| !FindChildForm(GroupReader, DtlaForm, TEXT("0007"), Form0007)
				|| !FindChildForm(GroupReader, Form0007, TEXT("DATA"), DataForm))
			{
				UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: %s missing DTLA/0007/DATA structure"), *MeshGroupPath);
				continue;
			}
			for (const FSWGIffChunk& Child : GroupReader.ReadChildren(DataForm))
			{
				if (Child.Tag == TEXT("CHLD") && Child.DataSize > 4)
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

			// Temporary diagnostic: investigating the player's capsule
			// resizing to a degenerate ~1-unit half-height (see chat —
			// player ends up underground) — need the RAW decoded bind-pose
			// bounds per body part to see whether POSN data itself is
			// already near-flat (implying a missing bone-transform step) or
			// whether something later in the pipeline collapses it.
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
	// character/creature skin shaders (needing hue/palette variation) wrap it
	// as FORM CSHD > FORM 0001 > FORM SSHT — confirmed live via swg.DumpIffTree
	// on shader/hum_m_body.sht. Animated shaders (console screens, terminals,
	// bazaar signs — confirmed live via shader/anim_bazaar.sht) are a
	// completely different top-level FORM SWTS instead. Recurses a few levels
	// to find whichever TargetFormType form is present, regardless of what
	// (if anything) wraps it.
	bool FindFormRecursive(const FSWGIffReader& Reader, const FSWGIffChunk& Chunk, const FString& TargetFormType, FSWGIffChunk& OutForm, int32 MaxDepth)
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
		const TArray<FSWGIffChunk> VersionForms = FindChildForms(Reader, SwtsForm);
		if (VersionForms.Num() == 0)
		{
			return FString();
		}

		FString FirstTexturePath, MainTexturePath;

		for (const FSWGIffChunk& Child : Reader.ReadChildren(VersionForms[0]))
		{
			if (Child.IsForm() || Child.Tag != TEXT("TEXT"))
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
	if (!FindFormRecursive(Reader, TopLevel[0], TEXT("SSHT"), SshtForm, 3))
	{
		FSWGIffChunk SwtsForm;
		if (FindFormRecursive(Reader, TopLevel[0], TEXT("SWTS"), SwtsForm, 3))
		{
			FString Result = ResolveAnimatedShaderDiffuseTexturePath(Reader, SwtsForm);
			Result.ReplaceInline(TEXT("\\"), TEXT("/"));
			return Result;
		}

		UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: shader %s has no SSHT or SWTS form (top-level FORM %s)"), *ShaderVirtualPath, *TopLevel[0].FormType);
		return FString();
	}

	// SSHT's version-tagged wrapper form isn't always "0000" (same version-drift
	// pattern already hit for .trn's PTAT and .msh's MESH forms) — take
	// whichever single FORM child is actually present rather than hardcode it.
	const TArray<FSWGIffChunk> SshtChildForms = FindChildForms(Reader, SshtForm);
	if (SshtChildForms.Num() == 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: shader %s SSHT has no version form"), *ShaderVirtualPath);
		return FString();
	}
	const FSWGIffChunk& Form0000 = SshtChildForms[0];

	FSWGIffChunk TxmsForm;
	if (!FindChildForm(Reader, Form0000, TEXT("TXMS"), TxmsForm))
	{
		UE_LOG(LogTemp, Verbose, TEXT("USWGMeshGeneratorSubsystem: shader %s (version form %s) has no TXMS"), *ShaderVirtualPath, *Form0000.FormType);
		return FString();
	}

	FString FirstTexturePath, MainTexturePath;

	for (const FSWGIffChunk& TxmForm : Reader.ReadChildren(TxmsForm))
	{
		if (!TxmForm.IsForm() || TxmForm.FormType != TEXT("TXM "))
		{
			continue;
		}

		FSWGIffChunk InnerForm0001, DataChunk, NameChunk;
		if (!FindChildForm(Reader, TxmForm, TEXT("0001"), InnerForm0001)) continue;
		if (!FindChildChunk(Reader, InnerForm0001, TEXT("DATA"), DataChunk)) continue;
		if (!FindChildChunk(Reader, InnerForm0001, TEXT("NAME"), NameChunk)) continue;

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

UTexture2D* USWGMeshGeneratorSubsystem::GetOrLoadObjectTexture(const FString& TextureVirtualPath)
{
	if (TextureVirtualPath.IsEmpty())
	{
		return nullptr;
	}

	if (TObjectPtr<UTexture2D>* Existing = LoadedObjectTextures.Find(TextureVirtualPath))
	{
		return *Existing;
	}

	UTexture2D* Result = nullptr;
	if (TreSubsystem && TreSubsystem->FileExists(TextureVirtualPath))
	{
		const TArray<uint8> Bytes = TreSubsystem->ExtractFile(TextureVirtualPath);
		Result = FSWGDDSTextureLoader::LoadTexture2D(Bytes, FName(*TextureVirtualPath), /*bSRGB=*/true);
	}

	if (!Result)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to load object texture '%s'"), *TextureVirtualPath);
	}

	LoadedObjectTextures.Add(TextureVirtualPath, Result);
	return Result;
}

UMaterialInterface* USWGMeshGeneratorSubsystem::GetOrBuildObjectMaterial(const FString& ShaderVirtualPath)
{
	if (ShaderVirtualPath.IsEmpty())
	{
		return nullptr;
	}

	if (TObjectPtr<UMaterialInterface>* Existing = ObjectMaterialCache.Find(ShaderVirtualPath))
	{
		return *Existing;
	}

	UMaterialInterface* Result = nullptr;
	const FString TexturePath = ResolveShaderDiffuseTexturePath(ShaderVirtualPath);
	UTexture2D* Texture = GetOrLoadObjectTexture(TexturePath);

	if (!Texture)
	{
		// Temporary diagnostic: pin down exactly which shaders' texture
		// resolution is silently failing (see chat: item submeshes showing
		// partial N/M textured ratios with no other warning logged at all —
		// meaning the failure is inside ResolveShaderDiffuseTexturePath's own
		// TXM-tag loop, not any of its already-logged early-outs).
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

		if (ObjectMaterialParent)
		{
			if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(ObjectMaterialParent, this))
			{
				MID->SetTextureParameterValue(TEXT("Diffuse"), Texture);
				Result = MID;
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: M_SWGObjectTextured not found — falling back to tint material for shader '%s'"), *ShaderVirtualPath);
		}
	}

	ObjectMaterialCache.Add(ShaderVirtualPath, Result);
	return Result;
}

UMeshComponent* USWGMeshGeneratorSubsystem::BuildDynamicMesh(AActor& Actor, const FSWGMeshData& MeshData)
{
	check(IsInGameThread());

	if (MeshData.Submeshes.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGMeshGeneratorSubsystem: mesh data has no submeshes — refusing to build"));
		return nullptr;
	}

	UDynamicMeshComponent* MeshComponent = NewObject<UDynamicMeshComponent>(&Actor, NAME_None, RF_Transactional);

	// Each FSWGMeshSubmesh has its own self-contained vertex buffer (Triangles
	// indexes into it, not a shared global one) — appended into one combined
	// FDynamicMesh3 here, one material ID per submesh so a real per-shader
	// material can be assigned later without restructuring the mesh.
	const FVector3f PlaceholderColor = GetPlaceholderColorForActor(Actor);

	MeshComponent->EditMesh([&MeshData, PlaceholderColor](FDynamicMesh3& EditMesh)
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
	});

	TArray<UMaterialInterface*> SubmeshMaterials;
	SubmeshMaterials.Reserve(MeshData.Submeshes.Num());
	for (const FSWGMeshSubmesh& Submesh : MeshData.Submeshes)
	{
		SubmeshMaterials.Add(GetOrBuildObjectMaterial(Submesh.ShaderName));
	}

	FinalizeMeshComponent(Actor, *MeshComponent, PlaceholderColor, SubmeshMaterials);
	return MeshComponent;
}

UMeshComponent* USWGMeshGeneratorSubsystem::BuildDynamicMesh(AActor& Actor, UDynamicMesh* DynamicMesh, const TArray<FString>& ShaderNames)
{
	check(IsInGameThread());

	if (!DynamicMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGMeshGeneratorSubsystem: No Dynamic Mesh"));
		return nullptr;
	}

	// Cache-hit path (see ProcessNextRequest) — geometry/attributes come from
	// the serialized UDynamicMesh, but ShaderNames (persisted alongside it in
	// the same cache file) lets this build real per-submesh materials exactly
	// like the fresh-parse path, instead of falling back to a single tint.
	const FVector3f PlaceholderColor = GetPlaceholderColorForActor(Actor);

	UDynamicMeshComponent* MeshComponent = NewObject<UDynamicMeshComponent>(&Actor, NAME_None, RF_Transactional);
	MeshComponent->SetDynamicMesh(DynamicMesh);

	TArray<UMaterialInterface*> SubmeshMaterials;
	SubmeshMaterials.Reserve(ShaderNames.Num());
	for (const FString& ShaderName : ShaderNames)
	{
		SubmeshMaterials.Add(GetOrBuildObjectMaterial(ShaderName));
	}

	FinalizeMeshComponent(Actor, *MeshComponent, PlaceholderColor, SubmeshMaterials);
	return MeshComponent;
}

void USWGMeshGeneratorSubsystem::FinalizeMeshComponent(AActor& Actor, UDynamicMeshComponent& MeshComponent, const FVector3f& PlaceholderColor, const TArray<UMaterialInterface*>& SubmeshMaterials)
{
	// Fallback for any submesh whose shader is empty/unresolved/texture-load
	// failed (SubmeshMaterials[i] == nullptr) — the engine's vertex-color-only
	// debug material instead of the flat default surface material, so
	// GetPlaceholderColorForActor's per-type tint (set via EnableVertexColors/
	// SetVertexColor) is still visible for anything that couldn't get a real
	// texture. The plain default material ignores vertex color entirely.
	UMaterialInterface* PlaceholderMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/EngineDebugMaterials/VertexColorViewMode_ColorOnly.VertexColorViewMode_ColorOnly"));
	if (!PlaceholderMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: failed to load VertexColorViewMode_ColorOnly, falling back to plain default material (no vertex color tint will show)"));
		PlaceholderMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	const int32 NumSlots = FMath::Max(SubmeshMaterials.Num(), 1);
	TArray<UMaterialInterface*> Materials;
	Materials.Reserve(NumSlots);
	int32 NumTextured = 0;
	for (int32 i = 0; i < NumSlots; ++i)
	{
		UMaterialInterface* Mat = SubmeshMaterials.IsValidIndex(i) ? SubmeshMaterials[i] : nullptr;
		if (Mat)
		{
			++NumTextured;
		}
		Materials.Add(Mat ? Mat : PlaceholderMaterial);
	}
	MeshComponent.ConfigureMaterialSet(Materials);

	UE_LOG(LogTemp, Warning, TEXT("USWGMeshGeneratorSubsystem: %s (class %s) assigned %d/%d real textured material(s), tint=(%.2f,%.2f,%.2f)"),
		*Actor.GetName(), *Actor.GetClass()->GetName(), NumTextured, NumSlots,
		PlaceholderColor.X, PlaceholderColor.Y, PlaceholderColor.Z);

	// Confirmed via FBaseDynamicMeshSceneProxy source: ColorOverrideMode
	// anything other than None (VertexColors/Polygroups/Constant) makes the
	// scene proxy completely IGNORE whatever material is actually assigned to
	// this component and force-substitute the engine's own built-in
	// "default vertex color" debug material instead — this was why every
	// real per-shader textured MaterialInstanceDynamic (correctly assigned,
	// confirmed via logs) never actually rendered: it was always being
	// silently replaced at the render-proxy level. Vertex color data itself
	// is still uploaded to the GPU regardless of this mode (only Constant
	// mode ignores it), so the VertexColorViewMode_ColorOnly fallback
	// material (used for any submesh whose shader didn't resolve) still
	// works correctly here with ColorMode left at its default None — it
	// reads vertex color through its own material graph, same as any other
	// assigned material would.

	MeshComponent.RegisterComponent();

	if (!Actor.GetRootComponent())
	{
		Actor.SetRootComponent(&MeshComponent);
	}
	else
	{
		MeshComponent.AttachToComponent(Actor.GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

		// ASWGCreature/ASWGPlayer are ACharacters — their root is the capsule,
		// centered above the feet, but SWG geometry is authored with its origin
		// at ground level. Without this offset the decoded mesh renders at the
		// capsule's center (network Z + half-height), floating above the actual
		// ground/network position. This is the Blueprint third-person template's
		// mesh offset convention, which the base C++ ACharacter never sets up.
		//
		// The capsule itself also gets resized to the real mesh bounds here
		// (rather than staying at UE's flat default 88 half-height/34 radius)
		// — every offset derived from the capsule (this one, the camera's eye
		// height, the nameplate's head clearance) was wrong for any creature
		// whose true size differs from a default human, which is most of
		// them. Centering the mesh on the resized capsule this way keeps the
		// mesh's own vertical midpoint exactly at the root regardless of
		// whether its geometry happens to be authored with Z=0 at the feet.
		//
		// Bounds are read back from the component's own DynamicMesh here
		// (rather than from FSWGMeshData, which the cache-hit path doesn't
		// have) so the fresh-parse and cache-hit paths compute this identically
		// — one source of truth instead of two independent implementations.
		if (ACharacter* Character = Cast<ACharacter>(&Actor))
		{
			if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				FBox MeshBounds(ForceInit);
				MeshComponent.GetDynamicMesh()->ProcessMesh([&MeshBounds](const FDynamicMesh3& Mesh)
					{
						for (const int32 VertexID : Mesh.VertexIndicesItr())
						{
							MeshBounds += (FVector)Mesh.GetVertex(VertexID);
						}
					});

				if (MeshBounds.IsValid)
				{
					// Read the server's real feet-level Z back from
					// ASWGCreature::LastNetworkZ (set directly by
					// USWGObjectGraphSubsystem::GroundedLocationFor) instead
					// of reverse-engineering it as "CurrentLocation.Z -
					// OldHalfHeight". That back-calculation assumed nothing
					// else ever moves the actor between spawn and whenever
					// its mesh happens to finish building — false in
					// practice: the async mesh queue can take many seconds,
					// and an unconstrained freefall before real terrain
					// collision exists (MOVE_Flying's per-tick
					// re-assertion — see ASWGPlayer::Tick — only stops a
					// fall already in progress, it doesn't undo the
					// distance already dropped before Tick first caught it)
					// corrupts CurrentLocation.Z in the meantime. Confirmed
					// live as the actual cause of the player ending up
					// deep underground: the back-calculated "NetworkZ" came
					// out ~54 units below the server's real value because
					// the capsule-resize step ran long after the character
					// had already fallen and been caught mid-drop.
					const ASWGCreature* Creature = Cast<ASWGCreature>(&Actor);
					const float NetworkZ = Creature ? Creature->LastNetworkZ : (Actor.GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight());

					const FVector Extent = MeshBounds.GetExtent();
					const float NewHalfHeight = FMath::Max(Extent.Z, 1.0f);
					const float NewRadius = FMath::Max(FMath::Max(Extent.X, Extent.Y), 1.0f);
					Capsule->SetCapsuleSize(NewRadius, NewHalfHeight);

					FVector CorrectedLocation = Actor.GetActorLocation();
					CorrectedLocation.Z = NetworkZ + NewHalfHeight;
					Actor.SetActorLocation(CorrectedLocation);

					MeshComponent.SetRelativeLocation(FVector(0.0f, 0.0f, -MeshBounds.GetCenter().Z));

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
					MeshComponent.SetRelativeLocation(FVector(0.0f, 0.0f, -Capsule->GetScaledCapsuleHalfHeight()));
				}
			}
		}
	}

	// ASWGCreature is an ACharacter, which always carries its own default
	// USkeletalMeshComponent (ACharacter::GetMesh()) with no SkeletalMesh
	// assigned. We deliberately never feed our decoded geometry into that —
	// building a real runtime USkeletalMesh (skin weights, LOD render data)
	// needs the same editor-only import pipeline UDynamicMeshComponent exists
	// to avoid — so just hide it rather than leave an empty/default mesh
	// component rendering (or not) alongside ours.
	//
	// Tried copying CharacterMesh's relative transform here on the theory that
	// ACharacter offsets its default mesh down by the capsule half-height so
	// feet land on the floor — confirmed false (CharacterMesh->GetRelativeLocation()
	// is (0,0,0) too; that offset convention lives in the Blueprint third-person
	// template, not the base C++ ACharacter). The real "NPCs floating" fix is
	// the capsule-half-height correction applied where network position gets
	// set (see USWGObjectGraphSubsystem — SetActorLocation places the actor
	// origin, i.e. capsule center, at the network Z, but that Z means feet/
	// ground level).
	if (ACharacter* Character = Cast<ACharacter>(&Actor))
	{
		if (USkeletalMeshComponent* CharacterMesh = Character->GetMesh())
		{
			CharacterMesh->SetVisibility(false);
			CharacterMesh->SetHiddenInGame(true);
		}
	}

	OnMeshReady.Broadcast(&Actor, &MeshComponent);
}
