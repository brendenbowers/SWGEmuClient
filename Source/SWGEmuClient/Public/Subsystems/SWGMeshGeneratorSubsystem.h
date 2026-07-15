#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TRE/SWGMeshReader.h"
#include "TRE/SWGSkeletonReader.h"
#include "TRE/SWGRuntimeAnimationPlayer.h"
#include "SWGMeshGeneratorSubsystem.generated.h"

class USWGTreSubsystem;
class UMeshComponent;
class UDynamicMesh;
class UDynamicMeshComponent;
class UMaterialInterface;
class UTexture2D;
class UPoseableMeshComponent;

/**
 * One actor's live procedural animation playback — see
 * TryApplyGeneratedAnimatedMesh's comment for why this drives bones directly
 * every tick instead of using a built UAnimSequence.
 */
struct FSWGPlayingAnimation
{
	TWeakObjectPtr<UPoseableMeshComponent> PoseableMesh;
	FSWGSkeletonData Skeleton;

	/** Idle, walk, jog, run in that order. All are decoded once when the mesh is attached. */
	TArray<FSWGRuntimeAnimation> LocomotionAnimations;
	int32 ActiveLocomotionIndex = 0;
	float PlaybackTimeSeconds = 0.0f;
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

	/** .mgn (skeletal) needs FSWGMeshReader::ReadSkeletalMeshBindPose instead of
	 *  ReadStaticMesh — set by whichever resolution step determines the file kind. */
	bool bSkeletal = false;
};

/**
 * Processes the backlog of spawned entities that still need a mesh built and
 * attached — see world-object-plan.html "Mesh rendering": CRC -> template ->
 * appearance (.apt->.lod, or .sat->.lmg/.mgn) -> mesh file -> parse
 * (FSWGMeshReader, already implemented and engine-agnostic) -> decode -> build
 * UDynamicMeshComponent (runtime-safe, no editor-only mesh-build dependency;
 * .mgn is bind-pose only for now, no skinning/animation — see the design doc's
 * "Decision: bind-pose-only creatures/players for now").
 *
 * Empty skeleton for now — every step below is a stub, mirroring
 * USWGTerrainSubsystem's phased build-out. Filling these in is separate
 * follow-up work.
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
	bool ResolveMeshPath(uint32 TemplateCrc, TArray<FString>& OutMeshVirtualPaths, bool& bOutSkeletal);

	/** The path-based half of ResolveMeshPath, factored out so RequestMeshForTemplatePath can skip the CRC->path lookup. */
	bool ResolveMeshPathForTemplate(const FString& TemplatePath, TArray<FString>& OutMeshVirtualPaths, bool& bOutSkeletal);

	/** mg4: FSWGMeshReader::ReadStaticMesh/ReadSkeletalMeshBindPose — intended to run off the game thread, like USWGTerrainSubsystem::BakeHeightmap. */
	bool ParseMesh(const FSWGPendingMeshRequest& Request, FSWGMeshData& OutMeshData);

	/** mg5: builds/registers a UDynamicMeshComponent on Actor from the decoded FSWGMeshData (game thread) and broadcasts OnMeshReady. */
	UMeshComponent* BuildDynamicMesh(AActor& Actor, const FSWGMeshData& MeshData);

	/** Cache-hit equivalent of the above — same finalize path (material, capsule resize, OnMeshReady), just from an already-populated UDynamicMesh (loaded from USWGMeshGeneratorSubsystem's on-disk mesh cache) instead of freshly-decoded FSWGMeshData. ShaderNames is the per-submesh shader list persisted alongside the cached mesh (see ProcessNextRequest) — cache files predating this no longer parse (harmless, on-disk dev cache only; delete Saved/MeshCache to regenerate). */
	UMeshComponent* BuildDynamicMesh(AActor& Actor, UDynamicMesh* DynamicMesh, const TArray<FString>& ShaderNames);

	/**
	 * Shared tail of both BuildDynamicMesh overloads: material/color-mode
	 * setup, registration, attach-and-resize-capsule-to-mesh-bounds, hiding
	 * the default USkeletalMeshComponent, and broadcasting OnMeshReady.
	 * MeshBounds is always read back from MeshComponent's own DynamicMesh
	 * (not from FSWGMeshData) so both the fresh-parse and cache-hit paths
	 * compute it identically — one source of truth instead of two
	 * independent (and previously subtly different) implementations.
	 * SubmeshMaterials is one entry per submesh (real per-shader texture
	 * material from GetOrBuildObjectMaterial, or PlaceholderMaterial for any
	 * submesh whose shader failed to resolve/load a texture) — always sized
	 * to at least 1 by the caller.
	 */
	void FinalizeMeshComponent(AActor& Actor, UDynamicMeshComponent& MeshComponent, const FVector3f& PlaceholderColor, const TArray<UMaterialInterface*>& SubmeshMaterials);

	/**
	 * Hardcoded "for now, just the Wookiee" resolution: swaps in the pre-built
	 * SK_Wookiee skeletal mesh on a new UPoseableMeshComponent (not
	 * ACharacter::GetMesh()), hiding DynamicMeshComponent instead. Animation is
	 * driven directly at runtime (bones sampled from FSWGRuntimeAnimation every
	 * tick) rather than via a built UAnimSequence, since IAnimationDataController
	 * silently discards every keyframe in this engine build — see
	 * FSWGRuntimeAnimationPlayer's header comment. Returns false (no-op) for
	 * anything that isn't a recognized generated model.
	 */
	bool TryApplyGeneratedAnimatedMesh(AActor& Actor, const TArray<FString>& MeshVirtualPaths, UMeshComponent* DynamicMeshComponent);

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
	UTexture2D* GetOrLoadObjectTexture(const FString& TextureVirtualPath);

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

	/**
	 * Cache paths currently being written by an in-flight fresh-parse request —
	 * guards against two actors sharing a template CRC racing the same on-disk
	 * cache file. Any request whose target path is already in this set falls
	 * back to a plain parse-and-build without touching the cache.
	 */
	FCriticalSection CacheWriteLock;
	TSet<FString> InFlightCacheWrites;

	/** Parent material for GetOrBuildObjectMaterial's per-shader MIDs — a single Texture2D parameter ("Diffuse") plugged straight into BaseColor. Authored as a plugin content asset, not generated at runtime. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ObjectMaterialParent;

	/** TRE virtual texture path -> decoded transient UTexture2D. See GetOrLoadObjectTexture. */
	UPROPERTY()
	TMap<FString, TObjectPtr<UTexture2D>> LoadedObjectTextures;

	/** Shader virtual path (e.g. "shader/dl44_main_as9.sht") -> built MaterialInstanceDynamic. See GetOrBuildObjectMaterial. */
	UPROPERTY()
	TMap<FString, TObjectPtr<UMaterialInterface>> ObjectMaterialCache;

	/** Actors whose UPoseableMeshComponent is being driven directly every tick — see TryApplyGeneratedAnimatedMesh and FSWGRuntimeAnimationPlayer. */
	TArray<FSWGPlayingAnimation> PlayingAnimations;
};
