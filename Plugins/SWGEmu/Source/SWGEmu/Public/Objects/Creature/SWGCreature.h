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
class SWGEMU_API ASWGCreature : public ACharacter, public ISWGNetworkObjectInterface
{
	GENERATED_BODY()

public:
	ASWGCreature(const FObjectInitializer& ObjectInitializer);

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

	/** Convenience accessor — this IS the character's movement component (set via ObjectInitializer). */
	USWGMovementComponent* GetSWGMovementComponent() const;
};
