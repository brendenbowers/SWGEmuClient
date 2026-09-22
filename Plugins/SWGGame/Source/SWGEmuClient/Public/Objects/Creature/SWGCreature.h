#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "SWGCreature.generated.h"

class USWGTangibleComponent;
class USWGConditionComponent;
class USWGDefenderComponent;
class USWGHealthComponent;
class USWGSkillComponent;
class USWGEncumbranceComponent;
class USWGSpaceMissionComponent;
class USWGEquipmentComponent;
class USWGCombatStateComponent;
class USWGGroupComponent;
class USWGPerformanceComponent;
class USWGMovementComponent;
class ASWGCell;

/**
 * CREO — NPCs and player bodies. An ACharacter (not derived from ASWGObject,
 * since UE can't multi-inherit actor classes) implementing
 * ISWGNetworkObjectInterface directly. All CREO/TANO baseline data lives on
 * attached components — see world-object-plan.html "Component breakdown".
 *
 * The PLAY ("ghost"/profile) layer is NOT modeled here yet — deferred per the
 * "moveable player with health" milestone scope. When implemented, it attaches
 * as extra components on the player's own ASWGCreature, not a separate actor.
 */
UCLASS()
class SWGEMUCLIENT_API ASWGCreature : public ACharacter, public ISWGNetworkObjectInterface
{
	GENERATED_BODY()

public:
	ASWGCreature(const FObjectInitializer& ObjectInitializer);

	/** Bridges CombatStateComponent's posture/state changes into the movement component, which owns the speed/turn shaping those imply. */
	virtual void PostInitializeComponents() override;

	int64  SWGObjectId  = 0;
	uint32 SWGObjectCRC = 0;
	bool   bBaselinesComplete = false;

	virtual int64 GetObjectId() const override { return SWGObjectId; }
	virtual void SetObjectId(int64 NewObjectId) override { SWGObjectId = NewObjectId; }

	virtual uint32 GetObjectCrc() const override { return SWGObjectCRC; }
	virtual void SetObjectCrc(uint32 NewObjectCrc) override { SWGObjectCRC = NewObjectCrc; }

	// TANO (shared with ASWGItem)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGTangibleComponent> TangibleComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGConditionComponent> ConditionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGDefenderComponent> DefenderComponent;

	// CREO-only
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGSkillComponent> SkillComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGEncumbranceComponent> EncumbranceComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGSpaceMissionComponent> SpaceMissionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGEquipmentComponent> EquipmentComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGCombatStateComponent> CombatStateComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGGroupComponent> GroupComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	TObjectPtr<USWGPerformanceComponent> PerformanceComponent;

	// Loose fields — too small to warrant a component (see world-object-plan.html)
	int32 BankCredits    = 0;
	int32 CashCredits    = 0;
	int64 CreatureLinkId = 0; // Mount object id, 0 if none
	float Height         = 0.f;
	uint16 Level         = 0;
	int32 GuildId        = 0;

	/**
	 * The server's last-known feet/ground-level Z (set by
	 * USWGObjectGraphSubsystem::GroundedLocationFor). Read directly by
	 * USWGMeshGeneratorSubsystem's capsule-resize step rather than
	 * back-calculated as "CurrentLocation.Z - OldHalfHeight", which breaks if
	 * anything (e.g. freefall before terrain collision exists) moves the actor
	 * before its mesh finishes building.
	 */
	float LastNetworkZ = 0.0f;

	/** The cell this creature was composed into world space against (USWGObjectGraphSubsystem::ApplyContainment). */
	TWeakObjectPtr<ASWGCell> PlacedInCell;

	/**
	 * The mount this creature is currently riding, or null if on its own feet.
	 * Set/cleared by USWGObjectGraphSubsystem::ApplyRiderContainment when a
	 * Rider containment (ESWGContainmentType::Rider) attaches/detaches it —
	 * valid for any rider, local or remote, since the attach itself is purely
	 * visual. ASWGPlayer additionally reads this to forward its own steering
	 * input to the mount instead of moving itself.
	 */
	TWeakObjectPtr<ASWGCreature> RiddenMount;

	/**
	 * This mount's own "player" hardpoint, relative to its actor origin — set
	 * by USWGMeshGeneratorSubsystem::TryAttachVehicleBody once a vehicle's
	 * real body mesh (with named hardpoints) attaches, some time after the
	 * mount itself spawns. Unset for anything without a body hardpoint
	 * (ordinary creature mounts) — ApplyRiderContainment falls back to the
	 * skeletal "rider" socket, then the mesh root, in that case.
	 */
	TOptional<FTransform> RiderSeatTransform;

	/**
	 * How a rider sits on this mount — datatables/mount/rider_pose_map.iff's
	 * rider_pose for its saddle appearance ("vehicle_landspeeder",
	 * "saddle_body1_wide", ...), which picks the rider's loop_riding clip.
	 * Set by USWGMeshGeneratorSubsystem once the appearance resolves; empty
	 * for anything that isn't a mount.
	 */
	FString MountRiderPose;

	/**
	 * True while the transform is still the cell-relative spawn one. Cleared
	 * by cell placement, by a world-space UpdateTransform, or by finishing
	 * baselines with no container — after that a containment must not re-place.
	 */
	bool bAwaitingCellPlacement = false;

	/** Convenience accessor — this IS the character's movement component (set via ObjectInitializer). */
	USWGMovementComponent* GetSWGMovementComponent() const;
};
