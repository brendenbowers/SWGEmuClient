#pragma once
#include "Objects/World/SWGBuilding.h"
#include "Network/Objects/Zone/Ship/ShipObjectBaseline.h"
#include "TRE/SWGCockpitReader.h"
#include "SWGShip.generated.h"

/** POB ships reuse cell/door ownership while retaining independent flight state. */
UCLASS()
class SWGEMUCLIENT_API ASWGShip : public ASWGBuilding
{
	GENERATED_BODY()
public:
	FShipObjectBaseline ShipState;
	TOptional<FSWGCockpitData> CockpitData;
	bool bHasFlightBaseline = false;
	// Space pose is independent of the stationary frame used by the local POB's floors.
	FTransform SpacePose = FTransform::Identity;
	FVector SpaceVelocity = FVector::ZeroVector;
	FVector FlightAngularRates = FVector::ZeroVector; // pitch, yaw, roll; radians/second
	float FlightSpeed = 0.f; // scalar engine speed; slip velocity can lag behind it
	bool bPredictingLocalFlight = false;
	FTransform ReplicatedSpacePose = FTransform::Identity;
	bool bSmoothingSpacePose = false;
	void IntegrateFlight(float DeltaTime, float Throttle, const FVector& Steering);
	uint32 LastSpaceStamp = 0;
	bool bHasSpaceStamp = false;
};
