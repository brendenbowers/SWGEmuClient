#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TRE/SWGMeshReader.h"
#include "TRE/SWGSkeletonReader.h"
#include "TRE/SWGCustomizationIdManager.h"
#include "TRE/SWGAssetCustomizationManager.h"
#include "Customization/SWGCustomizationVariables.h"
#include "Engine/AssetUserData.h"
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
class IMeshUtilities;
class FSWGSkeletalAnimationPipeline;

/**
 * Attached to every generated UStaticMesh/USkeletalMesh (see
 * GetOrBuildGeneratedStaticMesh / GetOrBuildGeneratedSkeletalMesh) so a human
 * or debug tool inspecting the asset later — content browser, a console dump,
 * GetAssetUserData<>() — can tell which TRE source file(s) it was built from.
 * Generated asset names are just a hash (SM_<hash>/SK_<hash>), which alone
 * gives no way to identify the mesh.
 */
UCLASS()
class SWGEMUCLIENT_API USWGMeshSourceUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	/** Same DebugName string already logged at build time — usually the resolved TRE mesh virtual path(s), sometimes with an owning actor class appended. */
	UPROPERTY(VisibleAnywhere, Category = "SWGEmu")
	FString DebugName;
};

/** One material slot/section's occlusion zone names — see
 *  USWGMeshOcclusionZoneData. Wrapped in its own USTRUCT because UPROPERTY
 *  doesn't support a bare TArray<TArray<FString>>. */
USTRUCT()
struct FSWGMeshSectionOcclusionZones
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FString> ZoneNames;
};

/**
 * Attached to every generated USkeletalMesh whose source .mgn part(s) carried
 * OZN/ZTO/OZC data (see FSWGMeshSubmesh::OcclusionZoneNames) — one entry per
 * material slot/section, same order as the mesh's own Materials array.
 * Confirmed against real TRE data: hum_m_body.mgn's single section occupies
 * {chest, torso_f, torso_b, waist_f, waist_b, l_arm, r_arm, l_thigh,
 * r_thigh, l_shin, r_shin, l_foot, r_foot}, and a composite chest plate's own
 * single section occupies {chest, torso_f, torso_b} — an exact subset. A
 * body section is hidden (see USWGEquipmentComponent's occlusion-zone
 * reconciliation) once every one of its own zone names is also covered by
 * some currently-equipped item's zones — never on a partial match, since a
 * body mesh section here is typically one combined torso+limbs region with
 * no finer boundary in the source data to hide only part of it.
 */
UCLASS()
class SWGEMUCLIENT_API USWGMeshOcclusionZoneData : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FSWGMeshSectionOcclusionZones> ZoneNamesBySection;
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

	/** Wire containmentType (see SWGContainmentType.h) — only meaningful for
	 *  item-mesh requests (OnItemMeshReady/OnItemSkeletalMeshReady set below).
	 *  RequestItemMesh already uses it to skip enqueueing entirely for
	 *  non-appearance-related slots (see ResolveArrangementSlotNames/
	 *  IsAnySlotAppearanceRelated); kept here too for diagnostics. */
	int32 ContainmentType = 0;

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

	/** The .sat/.apt path resolved along the way to MeshVirtualPaths (see
	 *  ResolveMeshPathForTemplate) — also the exact string Core3 hashes as
	 *  the ACST lookup key for this object's customization variables (see
	 *  FSWGAssetCustomizationManager). Empty for requests that arrive with
	 *  MeshVirtualPaths already resolved by the caller. */
	FString AppearancePath;

	/**
	 * Set instead of Actor for actor-less item-mesh requests (see
	 * RequestItemMesh) — a request carrying this has no Actor at all
	 * (TWeakObjectPtr stays unset). ProcessNextRequest checks this before its
	 * usual Actor-validity check: once path resolution determines bSkeletal
	 * (see RequestItemMesh — the caller doesn't know this up front, since it
	 * depends on the item's own appearance-chain extension), it builds either
	 * the static mesh (BuildItemStaticMeshAssets — the same actor-agnostic
	 * asset half BuildGeneratedMeshComponent uses internally) and invokes
	 * this, or the skeletal mesh (SkeletalAnimationPipeline->
	 * RequestGeneratedSkeletalMesh) and invokes OnItemSkeletalMeshReady
	 * instead — exactly one of the two ever fires. Mesh is nullptr on any
	 * resolve/parse/build failure.
	 */
	TFunction<void(UStaticMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials)> OnItemMeshReady;

	/** Skeletal counterpart to OnItemMeshReady — see that field's comment.
	 *  Fires instead of OnItemMeshReady when the item's own appearance chain
	 *  resolves to a skeletal (.sat->.lmg->.mgn) rather than static
	 *  (.apt->.lod->.msh) mesh — e.g. wearable clothing/armor versus a held
	 *  weapon. Mesh is nullptr on any resolve/parse/build failure, or if the
	 *  item's mesh part(s) carry no embedded SKTM skeleton reference. */
	TFunction<void(USkeletalMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials)> OnItemSkeletalMeshReady;
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

	// FSWGSkeletalAnimationPipeline (owned via TUniquePtr below) needs
	// privileged access to TreSubsystem/MeshUtilities/GetOrBuildObjectMaterial
	// — see that class's own header comment for why this wasn't worth
	// promoting to a public API instead.
	friend class FSWGSkeletalAnimationPipeline;

public:
	// Declared (not defaulted) so the SkeletalAnimationPipeline raw-pointer
	// member below can be manually deleted in the .cpp, where the complete
	// FSWGSkeletalAnimationPipeline type is visible. Tried TUniquePtr first
	// (the usual pImpl idiom), but even with an explicit out-of-line
	// constructor+destructor, UHT's generated vtable-helper-ctor machinery
	// (DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER, embedded directly in this
	// header via GENERATED_BODY) still eagerly instantiated TUniquePtr's
	// templated destructor wherever this header is included — including
	// translation units with no visibility into the pipeline's complete
	// type ("deletion of pointer to incomplete type" from
	// Module.SWGEmuClient.cpp's unity blob). A raw pointer sidesteps that
	// entirely: it needs no special-member-function machinery at all, only
	// the explicit `delete` call in the destructor's body needs the
	// complete type, and that's only ever compiled in this class's own .cpp.
	virtual ~USWGMeshGeneratorSubsystem() override;

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

	/**
	 * Actor-less counterpart to RequestMesh(Actor, CrcClass) — resolves
	 * TemplateCrc, parses its mesh, and builds/caches either a UStaticMesh
	 * (weapons/held items — .apt->.lod->.msh appearance chain) or a
	 * USkeletalMesh (wearable clothing/armor — .sat->.lmg->.mgn chain) plus
	 * its live per-shader materials, without creating any component or
	 * requiring any AActor. Exactly one of OnStaticComplete/OnSkeletalComplete
	 * fires, on the game thread, once path resolution determines which kind
	 * this item's own appearance chain resolves to — the caller doesn't (and
	 * can't cheaply) know this up front, so there's no separate synchronous
	 * classify-first step. Mesh is nullptr on any resolve/parse/build failure
	 * (Materials empty in that case too). For callers like
	 * USWGEquipmentComponent that need a built mesh to attach themselves
	 * (e.g. to a specific character socket or via leader-pose-link) rather
	 * than have this subsystem attach it to an actor that doesn't exist for
	 * an individual equipped item. ContainmentType (see SWGContainmentType.h)
	 * is the item's wire containmentType — used to resolve which ARG
	 * (arrangement) group it occupies and skip the build entirely if none of
	 * that group's slot names are appearance-related (e.g. bank/inventory/
	 * mission_bag container slots — nothing should ever render for those).
	 * Neither callback fires in that case.
	 */
	void RequestItemMesh(uint32 TemplateCrc, int32 ContainmentType,
		TFunction<void(UStaticMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials)> OnStaticComplete,
		TFunction<void(USkeletalMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials)> OnSkeletalComplete);

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
	bool ResolveMeshPath(uint32 TemplateCrc, TArray<FString>& OutMeshVirtualPaths, TMap<FString, FString>& OutAnimationLatPaths, bool& bOutSkeletal, FString& OutAppearancePath);

	/** The path-based half of ResolveMeshPath, factored out so RequestMeshForTemplatePath can skip the CRC->path lookup. */
	bool ResolveMeshPathForTemplate(const FString& TemplatePath, TArray<FString>& OutMeshVirtualPaths, TMap<FString, FString>& OutAnimationLatPaths, bool& bOutSkeletal, FString& OutAppearancePath);

	/**
	 * CRC -> template -> arrangementDescriptorFilename (walking the DERV chain
	 * the same way ResolveMeshPathForTemplate does for appearanceFilename —
	 * they're frequently set at different DERV layers, e.g. a specific chest
	 * plate usually inherits arrangementDescriptorFilename from a shared base
	 * wearable template while defining its own appearanceFilename locally) ->
	 * ARGD -> the one ARG group ContainmentType selects
	 * (SWGGetArrangementGroupIndex, SWGContainmentType.h). Only meaningful for
	 * slotted equipment (SWGIsSlottedArrangement(ContainmentType)) — returns
	 * false otherwise, or if any step of the resolution fails.
	 */
	bool ResolveArrangementSlotNames(uint32 TemplateCrc, int32 ContainmentType, TArray<FString>& OutSlotNames);

	/**
	 * True if any of SlotNames has bIsAppearanceRelated set in
	 * abstract/slot/slot_definition/slot_definitions.iff (parsed once and
	 * cached — see SlotAppearanceRelatedByName). Non-appearance slots
	 * (bank/inventory/mission_bag and other pure containers) never render
	 * their contents on the character; RequestItemMesh uses this to skip
	 * building anything for items equipped there.
	 */
	bool IsAnySlotAppearanceRelated(const TArray<FString>& SlotNames);

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
	UMeshComponent* BuildGeneratedMeshComponent(AActor& Actor, const FSWGMeshData& MeshData, uint32 CacheHash, const FString& DebugName, float YawCorrectionDegrees, EComponentMobility::Type Mobility, const TMap<FString, FLinearColor>* PaletteTintOverrides = nullptr, const TMap<FString, int32>* TextureIndexOverrides = nullptr);

	/**
	 * The actor-agnostic asset-producing half of BuildGeneratedMeshComponent,
	 * factored out so RequestItemMesh can use it without an AActor.
	 * Gets/builds the cached UStaticMesh (GetOrBuildGeneratedStaticMesh — a
	 * cache hit does no geometry work at all) and resolves its live
	 * per-shader materials, one entry per MeshData.Submeshes in order,
	 * falling back to the same placeholder vertex-color material
	 * BuildGeneratedMeshComponent uses for any submesh whose shader
	 * failed/unresolved. Returns nullptr (OutMaterials left empty) if the
	 * static mesh itself couldn't be built.
	 */
	UStaticMesh* BuildItemStaticMeshAssets(const FSWGMeshData& MeshData, uint32 CacheHash, const FString& DebugName, const FVector3f& PlaceholderColor, const TMap<FString, FLinearColor>* PaletteTintOverrides, const TMap<FString, int32>* TextureIndexOverrides, TArray<UMaterialInterface*>& OutMaterials);

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

	/** One shader's fully-parsed customization bindings — see ResolveShaderCustomization. */
	struct FSWGShaderCustomization
	{
		/**
		 * Customization variable bare name (e.g. "index_color_1",
		 * "index_color_skin") -> the shader color-factor tag it feeds
		 * ("MAIN" = the base color the diffuse is modulated by, "HUEB" = the
		 * hue-blend color). This tag, NOT the variable name, is the real
		 * binding: the names are per-species (Wookiee uses index_color_1/
		 * index_color_3, Ithorian uses index_color_pattern/index_color_skin)
		 * but the tags are identical across every creature shader checked.
		 */
		TMap<FString, FString> ColorFactorTags;

		/** Ordered, 0-based list of selectable diffuse variants (FORM TXTR > CHUNK DATA). */
		TArray<FString> TextureVariants;

		/** Customization variable whose value indexes TextureVariants (FORM CUST > CHUNK TX1D). */
		FString TextureVariantVariable;

		/** Bare variable name bound to the given color-factor tag, or empty. */
		FString FindVariableForFactor(const FString& FactorTag) const
		{
			for (const TPair<FString, FString>& Pair : ColorFactorTags)
			{
				if (Pair.Value == FactorTag)
				{
					return Pair.Key;
				}
			}
			return FString();
		}
	};

	/**
	 * Parses a .sht's customization plumbing in one pass: FORM TFAC's PAL
	 * entries (variable -> color-factor tag) and FORM TXTR's explicit
	 * texture-variant list plus the FORM CUST > TX1D binding that says which
	 * variable indexes it. This replaces two earlier heuristics that both
	 * turned out wrong against real data — guessing primary/secondary tint
	 * from variable-name substrings (backwards for the Ithorian, whose
	 * "index_color_pattern" is MAIN, not the secondary), and deriving the
	 * selected texture by rewriting a "_sNN" suffix on the shader's default
	 * path (off by one: index 16 into the Wookiee's 25-entry list is
	 * wke_pattern_s17.dds, not _s16). Everything here is read from the
	 * shader itself, so it works for any species without special-casing.
	 */
	FSWGShaderCustomization ResolveShaderCustomization(const FString& ShaderVirtualPath);

	/**
	 * Loads/decodes a .pal customization palette (standard little-endian RIFF
	 * "PAL data" format — PALETTEHEADER{u16 Version; u16 NumEntries;} +
	 * NumEntries*{R,G,B,Flags} bytes — distinct from every other SWG asset
	 * format read elsewhere in this codebase, which are all big-endian IFF)
	 * and returns the average of all its entries as a flat tint approximation
	 * — used when no real per-character index is available (see
	 * ResolveCustomizationPaletteTints for the real pick). Returns opaque
	 * white (no tint change) if the palette can't be loaded/parsed.
	 */
	FLinearColor LoadPaletteAverageTint(const FString& PaletteVirtualPath);

	/** Same file format as LoadPaletteAverageTint, but returns the single
	 *  entry at Index (clamped to the palette's actual entry count) instead
	 *  of averaging all of them — the real per-character customization
	 *  color. Returns opaque white if the palette can't be loaded/parsed. */
	FLinearColor LoadPaletteColorAtIndex(const FString& PaletteVirtualPath, int32 Index);

	/**
	 * Loads (once, cached — see CustomizationIdManager/AssetCustomizationManager)
	 * the two customization IFF tables and resolves Customization's decoded
	 * (TypeId -> value) pairs into a PaletteFileName -> real indexed
	 * FLinearColor map for every PALETTE-type variable ACST associates with
	 * AppearancePath (skin/hair/eye color, etc. — see
	 * FSWGAssetCustomizationManager's header comment for the full
	 * TypeId->name->effect resolution chain). Only palette variables produce
	 * an entry here; RANGE variables (face/body morph sliders) aren't
	 * texture/material changes and aren't handled by this — see
	 * GetOrBuildObjectMaterial for how the result plugs into material
	 * building. Empty if AppearancePath has no ACST entry, or Customization
	 * has no palette-variable values set.
	 */
	TMap<FString, FLinearColor> ResolveCustomizationPaletteTints(const FSWGCustomizationVariables& Customization, const FString& AppearancePath);

	/**
	 * Same resolution chain as ResolveCustomizationPaletteTints, but for
	 * RANGE (non-palette) variables — the face/body morph sliders
	 * (blend_fat, blend_cheeks_0, etc.) — normalized from Customization's
	 * decoded raw value to [0,1] against the variable's ACST-defined
	 * [Min,Max], and keyed by the bare morph target name (the customization
	 * variable's path stripped down to its last path segment) matching
	 * FSWGMeshReader::ReadBlendTargets'/FSkeletalMeshImportData's naming —
	 * see TryApplyGeneratedAnimatedMesh for where this gets applied via
	 * USkeletalMeshComponent::SetMorphTarget. Empty for any variable this
	 * object never customized (left at the mesh's built-in zero-weight
	 * default) or whose ACST range is degenerate (Max <= Min).
	 */
	TMap<FString, float> ResolveCustomizationMorphWeights(const FSWGCustomizationVariables& Customization, const FString& AppearancePath);

	/**
	 * Same resolution chain again, but for RANGE variables whose bare name
	 * starts with "index_texture" — these don't blend a mesh shape at all,
	 * they pick which numbered variant texture a shader uses (SWG's
	 * "wke_pattern_s01.dds".."wke_pattern_s25.dds" fur-pattern family is the
	 * motivating case: the .sht bakes in one fixed default like "_s25", and
	 * the customization variable is what actually chooses the real per-
	 * character variant at runtime — see the shader's own FORM TXTR/FORM CUST
	 * chunks, which we don't parse; this reproduces the same result via the
	 * "_sNN" naming convention instead, see GetOrBuildObjectMaterial). Value
	 * is the raw decoded integer (not normalized — it's a literal index, 1-25
	 * for the pattern family), keyed by bare variable name (e.g.
	 * "index_texture_1"). Empty for any variable never customized.
	 */
	TMap<FString, int32> ResolveCustomizationTextureIndices(const FSWGCustomizationVariables& Customization, const FString& AppearancePath);

	/** Lazy-loads CustomizationIdManager/AssetCustomizationManager once per
	 *  session (shared by ResolveCustomizationPaletteTints and
	 *  ResolveCustomizationMorphWeights) — a no-op after the first call. */
	void EnsureCustomizationManagersLoaded();

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
	 *
	 * PaletteTintOverrides (see ResolveCustomizationPaletteTints) is checked
	 * against the shader's own TFAC palette path (ResolveShaderTintPalettePath)
	 * — if it names an override, that real per-character color is used
	 * instead of LoadPaletteAverageTint's flat approximation, and the result
	 * is NOT added to ObjectMaterialCache (it's specific to this one actor's
	 * customization, not reusable shader-wide like the cached/average case).
	 */
	UMaterialInterface* GetOrBuildObjectMaterial(const FString& ShaderVirtualPath, const TMap<FString, FLinearColor>* PaletteTintOverrides = nullptr, const TMap<FString, int32>* TextureIndexOverrides = nullptr);

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> TreSubsystem;

	TArray<FSWGPendingMeshRequest> PendingRequests;

	/** Parent material for GetOrBuildObjectMaterial's per-shader MIDs — a single Texture2D parameter ("Diffuse") plugged straight into BaseColor. Authored as a plugin content asset, not generated at runtime. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ObjectMaterialParent;

	/** Same as ObjectMaterialParent but BLEND_Masked, with OpacityMask wired from Diffuse's alpha channel — used instead of ObjectMaterialParent for shaders where FSWGShaderData::NeedsAlphaBlend() is true (see GetOrBuildObjectMaterial). */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ObjectMaterialParentMasked;

	/** TRE virtual texture path -> decoded transient UTexture2D. See GetOrLoadObjectTexture. */
	UPROPERTY()
	TMap<FString, TObjectPtr<UTexture2D>> LoadedObjectTextures;

	/** Shader virtual path (e.g. "shader/dl44_main_as9.sht") -> built MaterialInstanceDynamic. See GetOrBuildObjectMaterial. */
	UPROPERTY()
	TMap<FString, TObjectPtr<UMaterialInterface>> ObjectMaterialCache;

	IMeshUtilities* MeshUtilities = nullptr;

	/** Loaded once on first use (see ResolveCustomizationPaletteTints) from
	 *  customization/customization_id_manager.iff and
	 *  customization/asset_customization_manager.iff — both are small,
	 *  session-global reference tables, not per-object data. */
	bool bCustomizationManagersLoaded = false;
	FSWGCustomizationIdManager CustomizationIdManager;
	FSWGAssetCustomizationManager AssetCustomizationManager;

	/** Loaded once on first use (see IsAnySlotAppearanceRelated) from
	 *  abstract/slot/slot_definition/slot_definitions.iff — same small,
	 *  session-global reference table FSWGSkeletalAnimationPipeline::
	 *  GetSlotHardpoints loads independently for its own Name->Hardpoint
	 *  need; this one is Name->bIsAppearanceRelated instead. */
	bool bSlotAppearanceFlagsLoaded = false;
	TMap<FString, bool> SlotAppearanceRelatedByName;

	/** Owns the async skeletal-mesh + locomotion-animation generation
	 *  pipeline (RequestGeneratedSkeletalMesh/RequestLocomotionAnimSequence
	 *  future-returning requests, GetOrBuildLocomotionBlendSpace,
	 *  TryApplyGeneratedAnimatedMesh — see that class for why it was split
	 *  out). Manually new'd in Initialize() (once TreSubsystem/MeshUtilities
	 *  are set) and deleted in ~USWGMeshGeneratorSubsystem() — raw pointer,
	 *  not TUniquePtr, see the destructor's own comment for why. */
	FSWGSkeletalAnimationPipeline* SkeletalAnimationPipeline = nullptr;
};
