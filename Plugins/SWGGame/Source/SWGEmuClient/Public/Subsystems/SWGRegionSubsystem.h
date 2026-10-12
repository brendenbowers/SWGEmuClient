#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Region/SWGRegionSet.h"
#include "SWGRegionSubsystem.generated.h"

class USWGObjectGraphSubsystem;
class USWGTerrainSubsystem;
class USWGTreSubsystem;

/**
 * Retail ClientRegionManager: the planet's datatables/clientregion rows as spheres, and which one the local
 * player stands in. The retail client draws nothing for them (the city enter/leave text is a server chat
 * message, city/city:city_enter_city), so this only tracks the region and broadcasts changes.
 */
UCLASS()
class SWGEMUCLIENT_API USWGRegionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Fires when the player's innermost region changes; either name may be empty (outside any region). */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRegionChanged, const FString& /*LeftName*/, const FString& /*EnteredName*/);
	FOnRegionChanged OnRegionChanged;

	const FSWGRegionSet& GetRegions() const { return Regions; }
	const FString& GetPlanet() const { return LoadedPlanet; }

	/** Localised name of the region at a raw (x, north) position, empty if none. */
	FString FindRegionName(const FVector2D& RawPosition, float* OutRadius = nullptr) const;

	/** Localised name of the region the player is in now, empty if none. */
	const FString& GetCurrentRegionName() const { return CurrentName; }

	/** Reloads the table when the planet changed. */
	void EnsureLoaded();

private:
	bool Tick(float DeltaSeconds);
	FString ResolveName(const FSWGClientRegion& Region) const;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	UPROPERTY()
	TObjectPtr<USWGTerrainSubsystem> Terrain;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	FSWGRegionSet Regions;
	FString LoadedPlanet;
	int32 CurrentIndex = INDEX_NONE;
	FString CurrentName;
	FTSTicker::FDelegateHandle TickHandle;
};
