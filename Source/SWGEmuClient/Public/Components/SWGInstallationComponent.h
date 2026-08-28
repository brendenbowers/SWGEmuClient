#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/Objects/Zone/Installation/InstallationObjectBaseline.h"
#include "Network/Objects/Zone/Installation/InstallationObjectDelta.h"
#include "SWGInstallationComponent.generated.h"

/** INSO/HINO base3 — running state and power draw for harvesters, factories and turrets. */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGInstallationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGInstallationComponent();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	bool bActive = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	float SurplusPower = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	float BasePowerRate = 0.f;

	bool bHasBase3 = false;

	void ApplyBase3(const FInstallationObjectBaseline& Baseline);
	void ApplyDelta3(const FInstallationObjectDelta& Delta);
};

/**
 * INSO/HINO base7 — the resource pool a harvester can draw from, its extraction
 * rates and the hopper it fills. Only harvesters populate the lists; other
 * installations send the same layout with them empty.
 */
UCLASS(ClassGroup=(SWGEmu), meta=(BlueprintSpawnableComponent))
class SWGEMUCLIENT_API USWGHarvesterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWGHarvesterComponent();

	/** Selectable resource pool, parallel arrays keyed by position. */
	TArray<uint64>  ResourceIds;
	TArray<FString> ResourceNames;
	TArray<FString> ResourceTypes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int64 ActiveResourceId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	bool bOperating = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 ExtractionRateDisplayed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	float ExtractionRateMax = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	float CurrentExtractionRate = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	float HopperSize = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	int32 HopperSizeMax = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWGEmu")
	uint8 ConditionPercent = 0;

	TSWGBaselineList<FHopperItem> Hopper;
	bool bHasBase7 = false;

	/** Name of the resource currently being extracted, or empty if none/unknown. */
	FString GetActiveResourceName() const;

	void ApplyBase7(const FInstallationObjectBaseline& Baseline);
	void ApplyDelta7(const FInstallationObjectDelta& Delta);
};
