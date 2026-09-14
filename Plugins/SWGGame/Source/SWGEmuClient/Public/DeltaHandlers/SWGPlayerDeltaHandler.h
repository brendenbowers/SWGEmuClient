#pragma once

#include "CoreMinimal.h"
#include "Subsystems/SWGDeltaHandlerRegistry.h"

/**
 * Handles PLAY deltas, onto the same components as the baselines.
 */
class SWGEMUCLIENT_API FSWGPlayerDeltaHandler final : public ISWGDeltaHandler
{
public:
	bool CanHandleDelta(const AActor& Actor, const FDeltasMessage& Msg) const override final;
	bool HandleDelta(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments& Args) override final;
};
