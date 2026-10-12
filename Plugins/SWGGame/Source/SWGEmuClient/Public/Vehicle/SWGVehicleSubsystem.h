#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TRE/SWGClientDataFileReader.h"
#include "TRE/SWGDataTableReader.h"
#include "SWGVehicleSubsystem.generated.h"

class USWGTreSubsystem;
class USWGVehicleDynamicsComponent;

/**
 * Gives every vehicle mount a USWGVehicleDynamicsComponent once its body mesh has attached, loading the
 * vehicle client data (clientdata/vehicle/*.iff, named by datatables/mount/saddle_appearance_map.iff).
 */
UCLASS()
class SWGEMUCLIENT_API USWGVehicleSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	/** Also brings up the region tracker, which has no other owner (game-instance subsystems are created on first use). */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Every vehicle with dynamics in this world. */
	TArray<USWGVehicleDynamicsComponent*> GetDynamics() const;

	/** The cached client data for a vehicle template, or null if the template isn't a vehicle with data. */
	TSharedPtr<const FSWGClientDataFile> FindClientData(uint32 TemplateCrc, FString& OutPath);

private:
	struct FVehicleData
	{
		FString Path;
		TSharedPtr<const FSWGClientDataFile> Data;
	};

	void AdoptVehicles();

	TMap<uint32, FVehicleData> DataByCrc;
	FSWGDataTableData SaddleAppearanceMap;
	bool bTriedTable = false;
	float SinceScanSeconds = 0.f;
};
