#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGIntangibleObjectSubsystem.generated.h"

class USWGNetworkSubsystem;
class USWGTreSubsystem;
struct FSWGNetMessage;

/** What's known about one ITNO (intangible object) — datapad contents, mostly. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGIntangibleEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Intangible")
	int64 ObjectId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Intangible")
	FString Name;
};

/**
 * Tracks ITNO (intangible) objects — schematics, mission items, deeds, the
 * datapad's own contents — which never get a spawned client actor (see
 * SWGFormTagMappings.csv's SITN row: "no visual actor - no world presence"),
 * so there's nowhere else client-side to capture their name.
 */
UCLASS()
class SWGEMUCLIENT_API USWGIntangibleObjectSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** What's known about ObjectId, or null if no ITNO baseline has arrived for it yet. */
	const FSWGIntangibleEntry* FindEntry(int64 ObjectId) const { return Entries.Find(ObjectId); }

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message);

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	FDelegateHandle MessageHandle;

	TMap<int64, FSWGIntangibleEntry> Entries;
};
