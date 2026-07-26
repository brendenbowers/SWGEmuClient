#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TRE/SWGMeshReader.h"
#include "TRE/SWGSkeletonReader.h"
#include "SWGMeshGeneratorSubsystem.generated.h"

class USWGTreSubsystem;
class UMeshComponent;
class UDynamicMesh;
class UDynamicMeshComponent;
class UMaterialInterface;
class UTexture2D;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class USkeleton;
class UAnimSequence;
class UBlendSpace;
class UAnimSingleNodeInstance;

/**
 * One actor's live animation playback — a real UBlendSpace played on
 * Character->GetMesh() via UAnimSingleNodeInstance, driven every tick by the
 * actor's current horizontal speed. See TryApplyGeneratedAnimatedMesh.
 */
struct FSWGPlayingAnimation
{
	TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
	TWeakObjectPtr<UAnimSingleNodeInstance> AnimInstance;
};

/** One entity waiting for its mesh to be resolved, parsed, and built. */
struct FSWGPendingMeshRequest
{
	TWeakObjectPtr<AActor> Actor;

	uint32 TemplateCrc = 0;

	/** Set instead of TemplateCrc for callers that already have the template's
	 *  virtual path directly (world-snapshot objects — static, client-known
	 *  content identified by path, never by CRC) — see RequestMeshForTemplatePath. */
	FString TemplatePath;

	/** .msh or .mgn TRE virtual path(s) — already resolved by the caller for
	 *  now; RequestMesh doesn't yet do the CRC->template->appearance->mesh-file
	 *  walk itself (see ResolveMeshPath). Usually one entry, but a humanoid
	 *  skeletal appearance's single MSGN chunk packs multiple null-terminated
	 *  body-part .lmg references (arms/body/hands/head) that each resolve to
	 *  their own final .mgn path — all of them need parsing and merging into
	 *  one combined mesh. */
	TArray<FString> MeshVirtualPaths;

	/** SAT LATX entries, keyed by the skeleton path they animate. */
	TMap<FString, FString> AnimationLatPaths;

	/** .mgn (skeletal) needs FSWGMeshReader::ReadSkeletalMeshBindPose instead of
	 *  ReadStaticMesh — set by whichever resolution step determines the file kind. */
	bool bSkeletal = false;

	/** True for world-snapshot objects (buildings, static props, terrain
	 *  decoration — placed once from the .ws file and never touched by a
	 *  network transform update), false for anything spawned through the live
	 *  object graph (creatures, players, items — can move). Drives the built
	 *  mesh component's Mobility (see BuildGeneratedMeshComponent) so
	 *  static-only content gets the engine's static-mobility
	 *  rendering/lighting path instead of paying Movable's per-frame
	 *  transform-update overhead. */
	bool bStatic = false;
};

/**
 * Processes the backlog of spawned entities that still need a mesh built and
 * attached — see world-object-plan.html "Mesh rendering": CRC -> template ->
 * appearance (.apt->.lod, or .sat->.lmg/.mgn) -> mesh file -> parse
 * (FSWGMeshReader, already implemented and engine-agnostic) -> decode -> build
 * a real, cached UStaticMesh (see GetOrBuildGeneratedStaticMesh /
 * BuildGeneratedMeshComponent; .mgn is bind-pose only for now, no
 * skinning/animation — see the design doc's "Decision: bind-pose-only
 * creatures/players for now").
 */
UCLASS()
class SWGEMUCLIENT_API USWGMeshGeneratorSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/**
	 * mg1: enqueues Actor to have MeshVirtualPath (a .msh or .mgn TRE path)
	 * parsed and built into a mesh component, processed on a later Tick.
	 * bSkeletal selects FSWGMeshReader::ReadSkeletalMeshBindPose over
	 * ReadStaticMesh for the parse step.
	 */
	void RequestMesh(AActor* Actor, const FString& MeshVirtualPath, bool bSkeletal = false);
	void RequestMesh(AActor* Actor, const uint32 CrcClass);

	/** Like RequestMesh(Actor, CrcClass), but for callers that already have the
	 *  template's virtual path (world-snapshot objects) instead of a CRC. */
	void RequestMeshForTemplatePath(AActor* Actor, const FString& TemplatePath);

	/** Broadcast once RequestMesh's actor has a built, attached mesh component. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMeshReady, AActor* /*Actor*/, UMeshComponent* /*MeshComponent*/);
	FOnMeshReady OnMeshReady;

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMeshError, AActor* /*Actor*/, const FString& /*ErrorMessage*/);
	FOnMeshError OnMeshError;

private:
	/** mg2: pulls the next pending request off the queue and drives it through resolve -> parse -> build. */
	void ProcessNextRequest();

	/**
	 * mg3: CRC -> template -> appearance (.apt->.lod, or .sat->.lmg/.mgn) ->
	 * mesh virtual path. Not needed for requests that already arrive with a
	 * resolved MeshVirtualPath, but callers that only have a CRC/template will
	 * need this once it's implemented.
	 */
	bool ResolveMeshPath(uint32 TemplateCrc, TArray<FString>& OutMeshVirtualPaths, TMap<FString, FString>& OutAnimationLatPaths, bool& bOutSkeletal);

	/** The path-based half of ResolveMeshPath, factored out so RequestMeshForTemplatePath can skip the CRC->path lookup. */
	bool ResolveMeshPathForTemplate(const FString& TemplatePath, TArray<FString>& OutMeshVirtualPaths, TMap<FString, FString>& OutAnimationLatPaths, bool& bOutSkeletal);

	/** mg4: FSWGMeshReader::ReadStaticMesh/ReadSkeletalMeshBindPose — intended to run off the game thread, like USWGTerrainSubsystem::BakeHeightmap. */
	bool ParseMesh(const FSWGPendingMeshRequest& Request, FSWGMeshData& OutMeshData);

	/**
	 * Loads the once-built UStaticMesh for CacheHash (package name hashed
	 * under /Game/SWGEmu/Generated/SM_<hash> — see ProcessNextRequest for how
	 * static vs. dynamic requests compute this hash differently), or builds
	 * and saves it via GeometryScript's CopyMeshToStaticMesh from a scratch
	 * (unregistered, never-rendered) UDynamicMesh populated straight from
	 * MeshData if it doesn't exist yet and this is an editor/PIE build — same
	 * cache-or-build shape as GetOrBuildGeneratedSkeletalMesh. DebugName is
	 * just for logging. No materials are baked in (see
	 * BuildGeneratedMeshComponent for why); this only produces the geometry,
	 * with PlaceholderColor baked into its vertex colors as the fallback tint
	 * for any submesh whose real material can't be resolved later.
	 */
	UStaticMesh* GetOrBuildGeneratedStaticMesh(uint32 CacheHash, const FString& DebugName, const FSWGMeshData& MeshData, const FVector3f& PlaceholderColor);

	/**
	 * Gets/builds the cached UStaticMesh (see GetOrBuildGeneratedStaticMesh —
	 * a cache hit does no geometry work at all) and creates a
	 * UStaticMeshComponent for it with the given Mobility. Real per-shader
	 * materials (from GetOrBuildObjectMaterial) are assigned live on the
	 * component every time, never baked into the cached asset — those are
	 * UMaterialInstanceDynamic instances outered to this subsystem, not
	 * assets in the package, so a baked reference doesn't survive a reload.
	 * For an ACharacter (creatures/players), attaches under the existing
	 * capsule and resizes it to the mesh's real bounds (replacing the old
	 * flat 88/34 default), matching every capsule-derived offset (camera eye
	 * height, nameplate clearance) to it. For anything else (items, static
	 * world objects), replaces whatever default root Actor already has
	 * outright — a Static component can't attach under a Movable default
	 * root, and neither mobility has a capsule-derived reason to keep a
	 * separate root around — composing YawCorrectionDegrees
	 * (GetStaticMeshYawCorrection's per-file correction) on top of the old
	 * root's world transform (the actor's already-correct network spawn
	 * placement), since nothing else carries that placement once it's gone.
	 */
	UMeshComponent* BuildGeneratedMeshComponent(AActor& Actor, const FSWGMeshData& MeshData, uint32 CacheHash, const FString& DebugName, float YawCorrectionDegrees, EComponentMobility::Type Mobility);

	/**
	 * Generic per-species resolution: works for any ACharacter whose resolved
	 * mesh/skeleton data yields a SKTM skeleton reference and a usable LATX
	 * locomotion LAT. Swaps in the generated skeletal mesh (see
	 * GetOrBuildGeneratedSkeletalMesh) directly on Character->GetMesh() (no
	 * longer kept hidden), hiding ProceduralMeshComponent instead, and plays a
	 * generated UBlendSpace (see GetOrBuildLocomotionBlendSpace) via
	 * UAnimSingleNodeInstance. Returns false (no-op) for anything without a
	 * skeleton (non-animated actors) or whose generated assets aren't
	 * available.
	 */
	bool TryApplyGeneratedAnimatedMesh(AActor& Actor, const TArray<FString>& MeshVirtualPaths, const TMap<FString, FString>& AnimationLatPaths, UMeshComponent* ProceduralMeshComponent);

	/**
	 * Loads the once-built USkeletalMesh for this skeleton+mesh-parts
	 * combination (package name hashed from SkeletonPath + MeshVirtualPaths,
	 * under /Game/SWGEmu/Generated/), or builds and saves it via
	 * FSWGSkeletalMeshImporter if it doesn't exist yet and this is an
	 * editor/PIE build (see that class's WITH_EDITOR comment — there's no
	 * packaged-build-safe way to construct a real skinned mesh). The saved
	 * .uasset on disk *is* the cache — a later LoadObject call is the
	 * cache-hit path, same role GetOrBuildGeneratedStaticMesh's own cache
	 * plays for procedural meshes. If MeshVirtualPaths includes a "*_head*"
	 * part, also looks for a
	 * matching "<prefix>_face.skt" skeleton and merges it under the "head"
	 * joint for the mesh build only — the caller's own Skeleton (used to
	 * build locomotion animations) is left untouched since locomotion clips
	 * never animate face bones.
	 */
	USkeletalMesh* GetOrBuildGeneratedSkeletalMesh(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FSWGSkeletonData& Skeleton);

	/**
	 * Loads the once-built UAnimSequence for this skeleton+mesh-parts+clip
	 * combination (package name hashed from SkeletonPath + MeshVirtualPaths +
	 * ClipPath, under /Game/SWGEmu/Generated/ — MeshVirtualPaths is included
	 * because it's also part of GetOrBuildGeneratedSkeletalMesh's own hash:
	 * two species can share one SkeletonPath's source .skt data but still get
	 * two distinct generated USkeleton objects, one per generated mesh, so an
	 * anim sequence built against the wrong one would be silently
	 * incompatible with whichever mesh is actually on screen), or builds and
	 * saves it via FSWGAnimationImporter::BuildAnimSequence if it doesn't
	 * exist yet and this is an editor/PIE build. TargetSkeleton is the
	 * USkeleton the playing USkeletalMeshComponent actually uses
	 * (GeneratedMesh->GetSkeleton(), which may include merged face bones) —
	 * Skeleton is the plain joint list used to build the animated tracks
	 * themselves.
	 */
	UAnimSequence* GetOrBuildLocomotionAnimSequence(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const FString& ClipPath, const FSWGSkeletonData& Skeleton, USkeleton* TargetSkeleton);

	/**
	 * Loads the once-built UBlendSpace for this skeleton+mesh-parts+locomotion-clips
	 * combination (package name hashed the same way as
	 * GetOrBuildLocomotionAnimSequence, for the same reason — see its
	 * comment), or builds and saves it if missing and this is an editor/PIE
	 * build: a single horizontal-speed axis with idle/walk/run samples at
	 * 0/WalkSpeed/RunSpeed. AxisToScaleAnimation is set to that axis, so the
	 * engine scales each sample's playback rate by how far the live blend
	 * input is from its own sample speed — no manual play-rate bookkeeping
	 * needed at runtime.
	 */
	UBlendSpace* GetOrBuildLocomotionBlendSpace(const FString& SkeletonPath, const TArray<FString>& MeshVirtualPaths, const TArray<FString>& LocomotionPaths, UAnimSequence* IdleSequence, UAnimSequence* WalkSequence, UAnimSequence* RunSequence, float WalkSpeed, float RunSpeed, USkeleton* TargetSkeleton);

	/**
	 * Parses a .sht shader template (e.g. "shader/dl44_main_as9.sht") and
	 * returns the virtual path of its primary diffuse texture — confirmed
	 * wire layout: FORM SSHT > FORM 0000 > FORM TXMS > FORM TXM(*) > FORM
	 * 0001 > CHUNK DATA (first 4 bytes are a reversed-fourCC usage tag —
	 * "MAIN", "SPEC", etc.) + CHUNK NAME (the texture's full virtual path,
	 * e.g. "texture/dl44_main.dds" — already a complete, ready-to-load path,
	 * unlike terrain's SFAM table which only gave a bare name). Picks
	 * whichever TXM has the "MAIN" tag; falls back to the first TXM if none
	 * is tagged MAIN (some shaders only have one texture slot at all).
	 * Returns empty if the shader has no usable texture reference.
	 */
	FString ResolveShaderDiffuseTexturePath(const FString& ShaderVirtualPath);

	/**
	 * Parses a .sht shader template's FORM TFAC block (creature/player skin
	 * shaders only) and returns the virtual path of its customization color
	 * palette — wire layout: FORM SSHT > FORM 0000 > FORM TFAC > CHUNK "PAL "
	 * (repeated): [key string][4-byte reversed-fourCC tag][palette path].
	 * SWG creature skins are a pattern texture meant to be tinted by a color
	 * picked from this palette per-character. Returns empty if the shader has
	 * no TFAC block (plain object/weapon/building shaders — not an error).
	 */
	FString ResolveShaderTintPalettePath(const FString& ShaderVirtualPath);

	/**
	 * Loads/decodes a .pal customization palette (standard little-endian RIFF
	 * "PAL data" format — PALETTEHEADER{u16 Version; u16 NumEntries;} +
	 * NumEntries*{R,G,B,Flags} bytes — distinct from every other SWG asset
	 * format read elsewhere in this codebase, which are all big-endian IFF)
	 * and returns the average of all its entries as a flat tint approximation
	 * — a real per-character customization index pick is a follow-up, not
	 * implemented yet. Returns opaque white (no tint change) if the palette
	 * can't be loaded/parsed.
	 */
	FLinearColor LoadPaletteAverageTint(const FString& PaletteVirtualPath);

	/** Loads/decodes texture/<name>.dds once and caches the result (see LoadedObjectTextures) — same pattern as USWGTerrainSubsystem::GetOrLoadShaderTexture. */
	UTexture2D* GetOrLoadObjectTexture(const FString& TextureVirtualPath, bool bSRGB = true,
		bool bLegacyDXT5Normal = false);

	/**
	 * Builds (or returns an already-built) UMaterialInstanceDynamic for one
	 * shader (memoized by ShaderVirtualPath in ObjectMaterialCache — the same
	 * handful of shaders are reused across huge numbers of actors, e.g. every
	 * instance of one weapon or one wall type). Returns nullptr if the shader
	 * string is empty or its texture fails to resolve/load — caller falls
	 * back to the plain tint material for that submesh.
	 */
	UMaterialInterface* GetOrBuildObjectMaterial(const FString& ShaderVirtualPath);

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> TreSubsystem;

	TArray<FSWGPendingMeshRequest> PendingRequests;

	/** Parent material for GetOrBuildObjectMaterial's per-shader MIDs — a single Texture2D parameter ("Diffuse") plugged straight into BaseColor. Authored as a plugin content asset, not generated at runtime. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ObjectMaterialParent;

	/** TRE virtual texture path -> decoded transient UTexture2D. See GetOrLoadObjectTexture. */
	UPROPERTY()
	TMap<FString, TObjectPtr<UTexture2D>> LoadedObjectTextures;

	/** Shader virtual path (e.g. "shader/dl44_main_as9.sht") -> built MaterialInstanceDynamic. See GetOrBuildObjectMaterial. */
	UPROPERTY()
	TMap<FString, TObjectPtr<UMaterialInterface>> ObjectMaterialCache;

	/** Actors whose blend space's speed input needs updating every tick — see TryApplyGeneratedAnimatedMesh and Tick. */
	TArray<FSWGPlayingAnimation> PlayingAnimations;
};
