#pragma once

#include "CoreMinimal.h"
#include "Subsystems/SWGBaselineHandlerRegistry.h"

/**
 * Handles CREO baselines
 */
class SWGEMUCLIENT_API FSWGCreatureBaselineHandler final : public ISWGBaselineHandler
{
public:
	bool CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const override final;
	bool HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args) override final;
};
