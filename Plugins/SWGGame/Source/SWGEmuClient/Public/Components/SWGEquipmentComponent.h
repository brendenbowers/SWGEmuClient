#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Creature/CreatureObjectBaseline.h"
#include "Network/Objects/Zone/Creature/CreatureObjectDelta.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "Network/Objects/Zone/Creature/EquiptmentItem.h"
#include "TRE/SWGClientDataFileReader.h"
#include "SWGEquipmentComponent.generated.h"

struct FSWGPacket;
struct FSWGMeshData;
class USkeletalMesh;
class USkeletalMeshComponent;
class UMeshComponent;

/** CREO base6 — equipped items + composite appearance override. */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGEquipmentComponent();

	TSWGBaselineList<FEquiptmentItem> EquipmentList;
	FString AlternateAppearance;
	bool bHasBase6 = false;

	void ApplyBase6(const FCreatureObjectBaseline& Baseline);
	void ApplyDelta6(const FCreatureObjectDelta& Delta);

	/** Hydrates and renders equipment on a client-only character-select actor. */
	void SetPreviewEquipment(TArray<FEquiptmentItem> InEquipment, FString InAlternateAppearance);

	/**
	 * Equipment learned from a slotted UpdateContainmentMessage rather than
	 * CREO6 — the only way an NPC's outfit, weapon and hair ever arrive (see
	 * USWGObjectGraphSubsystem::SyncSlottedEquipment). A player's gear comes
	 * both ways; the CREO6 entry wins for the same ObjectId.
	 */
	void SetContainedItem(const FEquiptmentItem& Item);
	void RemoveContainedItem(uint64 ObjectId);

	/**
	 * Outfit baked into the owner's template .cdf (FORM WEAR) — how every
	 * "dressed_*" NPC is clothed; the server sends no wearable objects for
	 * them. Permanent for the actor's lifetime: never removed by
	 * RemoveUnequippedVisuals, since no equipment list ever names them.
	 */
	void SetClientDataWearables(const TArray<FSWGClientDataWearable>& Wearables);

	/** The mesh showing an equipped item on the body (worn or held), if it has one yet. */
	UMeshComponent* FindItemVisual(uint64 ObjectId) const;

protected:
	/**
	 * Keys into WearableComponentsByObjectId for SetClientDataWearables'
	 * meshes: ClientDataWearableIdBase | mesh index. Server object IDs are
	 * 48-bit, so the top bit can't collide.
	 */
	static constexpr uint64 ClientDataWearableIdBase = 1ull << 63;
	TSet<uint64> ClientDataWearableIds;

	/** EquipmentList merged with ContainedEquipment, one entry per ObjectId. */
	TArray<FEquiptmentItem> GatherCurrentEquipment() const;

	TMap<uint64, FEquiptmentItem> ContainedEquipment;

	/** Items with a mesh request already issued — see BuildEquipmentVisuals. */
	TSet<uint64> RequestedItemIds;

	/** Callers pass the full current equipment list: anything attached but absent from it is detached. */
	void BuildEquipmentVisuals(const TConstArrayView<FEquiptmentItem> CurrentEquipment);

	/** Destroys the attached meshes of items no longer equipped in CurrentEquipment. */
	void RemoveUnequippedVisuals(const TConstArrayView<FEquiptmentItem> CurrentEquipment);

	/**
	 * RetryCount: the character's own base skeletal mesh is a separate,
	 * independently-queued async build (see
	 * USWGMeshGeneratorSubsystem::ProcessNextRequest) — it can still be
	 * mid-flight when this item's mesh finishes.
	 */
	void AttachMeshToHardpoint(uint64 ObjectId, UStaticMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials, int32 RetryCount = 0);

	/**
	 * Wearable clothing/armor counterpart to AttachMeshToHardpoint — attaches
	 * via the engine's leader-pose link (SetLeaderPoseComponent) instead of a
	 * fixed hardpoint socket, so the wearable deforms with the body's live
	 * animated pose every tick with no per-tick pose code of our own. See
	 * character-equipment-slots-plan.html phase 4's "no UPoseableMeshComponent"
	 * design note for why this works even though the wearable's own SKTM
	 * (parsed by USWGMeshGeneratorSubsystem::RequestItemMesh) can be a
	 * different, smaller skeleton than the body's — the engine links leader/
	 * follower bones by matching FName, not by requiring a shared USkeleton
	 * asset, and every SWG SKTM the importer builds shares the same joint-name
	 * space. Same RetryCount reasoning as AttachMeshToHardpoint: the body mesh
	 * is a separate, independently-queued async build that can still be
	 * mid-flight when this item's mesh finishes.
	 */
	void AttachWearableSkeletalMesh(uint64 ObjectId, USkeletalMesh* Mesh, const TArray<UMaterialInterface*>& Materials, int32 RetryCount = 0);

	/**
	 * Hides bare-skin body-mesh sections covered by currently-attached
	 * wearables, and re-shows any that no longer are — called after every
	 * AttachWearableSkeletalMesh. Builds the union of occlusion zone names
	 * (see USWGMeshOcclusionZoneData) across every mesh in
	 * WearableComponentsByObjectId, then for each of the body mesh's own
	 * sections, hides it via ShowMaterialSection only if EVERY one of that
	 * section's zone names is in that union — never on a partial match (see
	 * USWGMeshOcclusionZoneData's own comment for why: a body section here is
	 * typically one combined torso+limbs region with no finer boundary in
	 * the source data to hide only part of it). No-op if the body mesh has
	 * no occlusion zone data at all (LOD levels below the one this was
	 * confirmed against, or non-humanoid skeletons).
	 */
	void ReconcileBodyOcclusion();

	/** Wearable skeletal components created by AttachWearableSkeletalMesh, keyed
	 *  by the equipped item's object ID — reused on re-equip so the same item
	 *  doesn't spawn a duplicate component. */
	UPROPERTY()
	TMap<uint64, TObjectPtr<USkeletalMeshComponent>> WearableComponentsByObjectId;

	/** Hardpoint components created by AttachMeshToHardpoint, keyed the same way
	 *  so a held item is reused rather than duplicated, and can be detached. */
	UPROPERTY()
	TMap<uint64, TObjectPtr<UStaticMeshComponent>> HardpointComponentsByObjectId;
};
