#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Network/Messages/Zone/Object/ObjectMenuRequest.h"
#include "TRE/SWGDataTableReader.h"
#include "SWGRadialMenuSubsystem.generated.h"

class USWGNetworkSubsystem;
class USWGObjectGraphSubsystem;
class USWGCommandSubsystem;
class USWGTreSubsystem;
struct FSWGNetMessage;

/** One option of a radial menu, resolved for display. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGRadialMenuItem
{
	GENERATED_BODY()

	/** 1-based position in the server's flattened list; 0 is the (implicit) root. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Radial")
	int32 Index = 0;

	/** Index of the option this one nests under; 0 for a top-level option. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Radial")
	int32 ParentIndex = 0;

	/** What the option means (Core3 RadialOptions / radial_menu.iff row). */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Radial")
	int32 RadialId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Radial")
	FText Label;

	/** The server wants an ObjectMenuSelect when this is picked (rather than a client command). */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Radial")
	bool bServerHandled = false;
};

/** A radial menu as received for one object. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGRadialMenu
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Radial")
	int64 ObjectId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Radial")
	TArray<FSWGRadialMenuItem> Items;

	/** Screen position the request was made from, so the UI can open where the player clicked. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Radial")
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	bool HasChildren(int32 Index) const
	{
		return Items.ContainsByPredicate([Index](const FSWGRadialMenuItem& Item) { return Item.ParentIndex == Index; });
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSWGOnRadialMenuReceived, const FSWGRadialMenu&, Menu);

/**
 * The object radial menu: asks the server what can be done with an object
 * (ObjectMenuRequest 0x146), hands the merged menu to the UI, and turns a
 * pick into either an ObjectMenuSelect or a client command.
 *
 * Retail's client contributes standard options itself (Examine, Attack,
 * ...) from datatables/player/radial_menu.iff; those are added here as
 * client-side items whose "command" column runs through
 * USWGCommandSubsystem. Everything the server adds carries a text
 * reference ("@ui_radial:item_pickup") and a callback flag, and is sent
 * back as a select.
 */
UCLASS()
class SWGEMUCLIENT_API USWGRadialMenuSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Asks the server for ObjectId's menu. OnMenuReceived fires with the result. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Radial")
	bool RequestMenu(int64 ObjectId, FVector2D ScreenPosition);

	/** Acts on a picked option of the last menu received for ObjectId. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Radial")
	void SelectOption(int64 ObjectId, int32 RadialId);

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Radial")
	FSWGOnRadialMenuReceived OnMenuReceived;

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message);

	/** The client-side options retail offers for every object of this kind. */
	void AppendClientDefaults(int64 ObjectId, TArray<FSWGRadialMenuItem>& Items) const;

	FText ResolveLabel(const FSWGRadialMenuEntry& Entry) const;

	/** datatables/player/radial_menu.iff, loaded on first use: row index == radial id. */
	const FSWGDataTableData* GetRadialTable() const;

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGCommandSubsystem> Commands;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	FDelegateHandle MessageHandle;

	/** Outstanding request: the counter we sent and where it was clicked. */
	uint8 NextCounter = 0;
	int64 PendingObjectId = 0;
	FVector2D PendingScreenPosition = FVector2D::ZeroVector;

	/** Last menu per object, so SelectOption knows whether an option is server-handled. */
	TMap<int64, FSWGRadialMenu> ReceivedMenus;

	mutable TUniquePtr<FSWGDataTableData> RadialTable;
	mutable bool bTriedRadialTable = false;
};
