#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"
#include "Network/Messages/Zone/Object/JtlShipMessages.h"
#include "SWGShipLaunchSubsystem.generated.h"

class USWGNetworkSubsystem;
class USWGObjectGraphSubsystem;
struct FSWGNetMessage;

struct FSWGLaunchShip
{
	int64 ShipId = 0;
	int64 ControlDeviceId = 0;
	FString Name;
	FString ParkingLocation;
	bool bCanLaunch = false;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnShipLaunchWindowRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnShipLaunchDataChanged);

UCLASS()
class SWGEMUCLIENT_API USWGShipLaunchSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	void Initialize(FSubsystemCollectionBase& Collection) override;
	void Deinitialize() override;
	UPROPERTY(BlueprintAssignable) FSWGOnShipLaunchWindowRequested OnWindowRequested;
	UPROPERTY(BlueprintAssignable) FSWGOnShipLaunchDataChanged OnDataChanged;
	UFUNCTION(BlueprintCallable) bool RequestShips(int64 TerminalId);
	UFUNCTION(BlueprintCallable) bool Launch(int64 ShipId);
	TArray<FSWGLaunchShip> GetShips() const;
	bool IsWaiting() const { return bWaiting; }
	const FString& GetStatus() const { return Status; }
	static int64 ResolveControlDevice(const USWGObjectGraphSubsystem& Graph, int64 ShipId);
private:
	void HandleMessage(TSharedPtr<FSWGNetMessage> Message);
	void HandleTimeout();
	UPROPERTY() TObjectPtr<USWGNetworkSubsystem> Network;
	FDelegateHandle MessageHandle;
	FTimerHandle RequestTimer;
	int64 RequestedTerminal = 0;
	bool bWaiting = false;
	FString Status;
	FJtlShipListResponse List;
};
