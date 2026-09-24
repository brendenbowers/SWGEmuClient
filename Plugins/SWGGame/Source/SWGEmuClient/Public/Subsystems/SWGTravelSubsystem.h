#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGTravelSubsystem.generated.h"

class USWGCommandSubsystem;
class USWGNetworkSubsystem;
class USWGTreSubsystem;
struct FSWGNetMessage;

USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGTravelDestination
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Travel")
	FString Planet;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Travel")
	FString Location;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Travel")
	FVector2D Position = FVector2D::ZeroVector;

	/** Starports and outposts can depart for another planet; shuttleports cannot. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Travel")
	bool bInterplanetary = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnTravelWindowRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnTravelDataChanged);

/** Owns the retail travel-terminal protocol and the data shown by the ticket window. */
UCLASS()
class SWGEMUCLIENT_API USWGTravelSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Travel")
	FSWGOnTravelWindowRequested OnTravelWindowRequested;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Travel")
	FSWGOnTravelDataChanged OnTravelDataChanged;

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Travel")
	const FString& GetDeparturePlanet() const { return DeparturePlanet; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Travel")
	const FString& GetDepartureLocation() const { return DepartureLocation; }

	/** Planets reachable from the current terminal, in retail table order. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Travel")
	TArray<FString> GetAvailablePlanets() const;

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Travel")
	TArray<FSWGTravelDestination> GetDestinations(const FString& Planet) const;

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Travel")
	int32 GetFare(const FString& ArrivalPlanet, bool bRoundTrip) const;

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Travel")
	bool CanBuyRoundTrip(const FSWGTravelDestination& Destination) const;

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Travel")
	bool PurchaseTicket(const FSWGTravelDestination& Destination, bool bRoundTrip);

	/** Wire arguments accepted by Core3's PurchaseTicketCommand. */
	static FString BuildPurchaseArguments(const FString& FromPlanet, const FString& FromLocation,
		const FString& ToPlanet, const FString& ToLocation, bool bRoundTrip);

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message);
	void LoadTravelTable();
	void RequestPlanetLocations();

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGCommandSubsystem> Commands;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	FDelegateHandle MessageHandle;
	FString DeparturePlanet;
	FString DepartureLocation;
	TArray<FString> PlanetOrder;
	TMap<FString, TMap<FString, int32>> Fares;
	TMap<FString, TArray<FSWGTravelDestination>> DestinationsByPlanet;
	bool bDepartureInterplanetary = false;
};
