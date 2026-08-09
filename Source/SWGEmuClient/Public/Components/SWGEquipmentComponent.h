#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Object/SWGBaselineListHelpers.h"
#include "Network/Objects/Zone/Creature/EquiptmentItem.h"
#include "SWGEquipmentComponent.generated.h"

struct FSWGPacket;
struct FSWGMeshData;
class USkeletalMesh;
class USkeletalMeshComponent;

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

	void ApplyBase6(FSWGPacket& Packet);
	void ApplyDelta6(FSWGPacket& Packet);

protected:
	void BuildEquipmentVisuals(const TConstArrayView<FEquiptmentItem> EquipmentList);

	/**
	 * RetryCount: the character's own base skeletal mesh is a separate,
	 * independently-queued async build (see
	 * USWGMeshGeneratorSubsystem::ProcessNextRequest) — it can still be
	 * mid-flight when this item's mesh finishes.
	 */
	void AttachMeshToHardpoint(UStaticMesh* Mesh, const FSWGMeshData MeshData, const TArray<UMaterialInterface*>& Materials, int32 RetryCount = 0);

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
};
