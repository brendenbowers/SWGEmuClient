#include "Components/SWGInstallationComponent.h"

USWGInstallationComponent::USWGInstallationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGInstallationComponent::ApplyBase3(const FInstallationObjectBaseline& Baseline)
{
	bActive = Baseline.ActiveFlag != 0;
	SurplusPower = Baseline.SurplusPower;
	BasePowerRate = Baseline.BasePowerRate;
	bHasBase3 = true;
}

void USWGInstallationComponent::ApplyDelta3(const FInstallationObjectDelta& Delta)
{
	if (Delta.ActiveFlag.IsSet())    { bActive = *Delta.ActiveFlag != 0; }
	if (Delta.SurplusPower.IsSet())  { SurplusPower = *Delta.SurplusPower; }
	if (Delta.BasePowerRate.IsSet()) { BasePowerRate = *Delta.BasePowerRate; }
}

USWGHarvesterComponent::USWGHarvesterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FString USWGHarvesterComponent::GetActiveResourceName() const
{
	const int32 Index = ResourceIds.IndexOfByKey(static_cast<uint64>(ActiveResourceId));
	return ResourceNames.IsValidIndex(Index) ? ResourceNames[Index] : FString();
}

void USWGHarvesterComponent::ApplyBase7(const FInstallationObjectBaseline& Baseline)
{
	ResourceIds = Baseline.ResourceIds;
	ResourceNames = Baseline.ResourceNames;
	ResourceTypes = Baseline.ResourceTypes;
	ActiveResourceId = Baseline.ActiveResourceId;
	bOperating = Baseline.bOperating != 0;
	ExtractionRateDisplayed = Baseline.ExtractionRateDisplayed;
	ExtractionRateMax = Baseline.ExtractionRateMax;
	CurrentExtractionRate = Baseline.CurrentExtractionRate;
	HopperSize = Baseline.HopperSize;
	HopperSizeMax = Baseline.HopperSizeMax;
	ConditionPercent = Baseline.ConditionPercent;
	Hopper = Baseline.Hopper;
	bHasBase7 = true;
}

void USWGHarvesterComponent::ApplyDelta7(const FInstallationObjectDelta& Delta)
{
	if (Delta.ActiveResourceId.IsSet())        { ActiveResourceId = *Delta.ActiveResourceId; }
	if (Delta.bOperating.IsSet())              { bOperating = *Delta.bOperating != 0; }
	if (Delta.ExtractionRateDisplayed.IsSet()) { ExtractionRateDisplayed = *Delta.ExtractionRateDisplayed; }
	if (Delta.ExtractionRateMax.IsSet())       { ExtractionRateMax = *Delta.ExtractionRateMax; }
	if (Delta.CurrentExtractionRate.IsSet())   { CurrentExtractionRate = *Delta.CurrentExtractionRate; }
	if (Delta.HopperSize.IsSet())              { HopperSize = *Delta.HopperSize; }
	if (Delta.HopperSizeMax.IsSet())           { HopperSizeMax = *Delta.HopperSizeMax; }
	if (Delta.ConditionPercent.IsSet())        { ConditionPercent = *Delta.ConditionPercent; }

	// The pool lists are plain arrays here rather than TSWGBaselineList, so they
	// go through a temporary to reuse the shared applier.
	auto ApplyToArray = [](const TSWGListChanges<uint64>& Changes, TArray<uint64>& Target)
	{
		TSWGBaselineList<uint64> Wrapper;
		Wrapper.Items = MoveTemp(Target);
		ApplyIndexedListChanges(Changes, Wrapper);
		Target = MoveTemp(Wrapper.Items);
	};
	auto ApplyToStringArray = [](const TSWGListChanges<FString>& Changes, TArray<FString>& Target)
	{
		TSWGBaselineList<FString> Wrapper;
		Wrapper.Items = MoveTemp(Target);
		ApplyIndexedListChanges(Changes, Wrapper);
		Target = MoveTemp(Wrapper.Items);
	};

	ApplyToArray(Delta.ResourceIds, ResourceIds);
	ApplyToStringArray(Delta.ResourceNames, ResourceNames);
	ApplyToStringArray(Delta.ResourceTypes, ResourceTypes);

	ApplyIndexedListChanges(Delta.Hopper, Hopper);
}
