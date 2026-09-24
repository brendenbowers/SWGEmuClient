#pragma once

#include "CoreMinimal.h"
#include "Network/Messages/Zone/MapLocationMessages.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGMapLocationSubsystem.generated.h"

class USWGNetworkSubsystem;
struct FSWGNetMessage;

DECLARE_MULTICAST_DELEGATE_OneParam(FSWGOnMapLocationsChanged, const FString&);

/** Requests and caches Core3's planetary map entries for both map views. */
UCLASS()
class SWGEMUCLIENT_API USWGMapLocationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RequestPlanet(const FString& Planet);
	const TArray<FSWGMapLocation>* GetLocations(const FString& Planet) const { return LocationsByPlanet.Find(Planet.ToLower()); }
	FSWGOnMapLocationsChanged OnLocationsChanged;

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message);

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	FDelegateHandle MessageHandle;
	TMap<FString, TArray<FSWGMapLocation>> LocationsByPlanet;
};
