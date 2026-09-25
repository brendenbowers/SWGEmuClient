#pragma once

#include "CoreMinimal.h"
#include "SWGInventoryQuery.generated.h"

class UGameInstance;

/** One inventory line, flattened for Blueprint. */
USTRUCT(BlueprintType)
struct SWGUI_API FSWGInventoryEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Inventory")
	int64 ObjectId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Inventory")
	FString Name;

	/** The equip slots it fills ("chest1", "hold_r"), joined with commas. Empty for a bag item. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Inventory")
	FString SlotNames;

	/** Stack size for resources, otherwise 0. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Inventory")
	int32 Quantity = 0;

	/** Name with the stack size appended, as the row shows it. */
	FString Label() const { return Quantity > 0 ? FString::Printf(TEXT("%s x%d"), *Name, Quantity) : Name; }

	bool operator==(const FSWGInventoryEntry& Other) const
	{
		return ObjectId == Other.ObjectId && Name == Other.Name && SlotNames == Other.SlotNames && Quantity == Other.Quantity;
	}
};

/**
 * Reads the local player's gear and bag out of the object graph's containment,
 * for whichever widget is showing it (the floating window or the gamepad dock).
 */
namespace SWGInventoryQuery
{
	/** Fills both lists, sorted (gear by slot, bag by name). True if either differs from what was passed in. */
	SWGUI_API bool Gather(UGameInstance* GameInstance, TArray<FSWGInventoryEntry>& Equipped, TArray<FSWGInventoryEntry>& Contents);

	SWGUI_API FSWGInventoryEntry Describe(UGameInstance* GameInstance, int64 ObjectId);

	/**
	 * How full the bag is, counted as Core3 counts it: every item in it and in
	 * the containers inside it (a crafting tool's contents aside), against its
	 * template's containerVolumeLimit. False until the bag has arrived.
	 */
	SWGUI_API bool GetBagCapacity(UGameInstance* GameInstance, int32& OutUsed, int32& OutLimit);
}
