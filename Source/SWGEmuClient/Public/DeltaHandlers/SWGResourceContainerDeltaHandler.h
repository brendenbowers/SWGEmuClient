#pragma once

#include "CoreMinimal.h"
#include "Subsystems/SWGDeltaHandlerRegistry.h"

/**
 * Handles RCNO deltas.
 */
class SWGEMUCLIENT_API FSWGResourceContainerDeltaHandler final : public ISWGDeltaHandler
{
public:
	bool CanHandleDelta(const AActor& Actor, const FDeltasMessage& Msg) const override final;
	bool HandleDelta(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments& Args) override final;
};
