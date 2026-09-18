#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGItemTransferSubsystem.generated.h"

class USWGCommandSubsystem;
class USWGObjectGraphSubsystem;
class USWGMeshGeneratorSubsystem;
class USWGTreSubsystem;

/**
 * Equip and unequip: moving an item between the player's inventory bag and
 * the player's own slots. Retail's server never does this on its own — "Use"
 * or "Equip" on a wearable is the client sending transferItemArmor /
 * transferItemWeapon / transferItemMisc with the player as the destination
 * and containmentType SlottedArrangementBase + arrangement group (Core3's
 * TransferItem*Command.h), so the slot choice is the client's too.
 */
UCLASS()
class SWGEMUCLIENT_API USWGItemTransferSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** The item's template has an arrangement descriptor — there is a slot it could go in. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Inventory")
	bool IsEquippable(int64 ObjectId) const;

	/** The item sits in one of the local player's slots right now. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Inventory")
	bool IsEquipped(int64 ObjectId) const;

	/**
	 * Equips from wherever the item is. Uses the first arrangement group whose
	 * slots are all free; if none is, the occupants of the first group are
	 * sent back to the inventory bag first. False if the item can't be
	 * equipped or nothing could be sent.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Inventory")
	bool EquipItem(int64 ObjectId);

	/** Moves an equipped item back into the inventory bag. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Inventory")
	bool UnequipItem(int64 ObjectId);

	/** The local player's inventory bag (template character_inventory), or 0 until its containment has arrived. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Inventory")
	int64 FindInventoryBagId() const;

private:
	/** transferItemArmor / transferItemWeapon / transferItemMisc, by template path — Core3 rejects the wrong one. */
	FString TransferCommandFor(int64 ObjectId) const;

	/** Slot name -> the item of the local player's that occupies it. */
	TMap<FString, int64> GatherOccupiedSlots(int64 PlayerId) const;

	void SendTransfer(int64 ObjectId, int64 DestinationId, int32 ContainmentType);

	UPROPERTY()
	TObjectPtr<USWGCommandSubsystem> Commands;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGMeshGeneratorSubsystem> MeshGenerator;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;
};
