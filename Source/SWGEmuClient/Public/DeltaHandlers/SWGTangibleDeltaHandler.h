#pragma once

#include "CoreMinimal.h"
#include "Subsystems/SWGDeltaHandlerRegistry.h"

/**
 * Handles TANO and WEAO deltas.
 */
class SWGEMUCLIENT_API FSWGTangibleDeltaHandler final : public ISWGDeltaHandler
{
public:
	bool CanHandleDelta(const AActor& Actor, const FDeltasMessage& Msg) const override final;
	bool HandleDelta(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments& Args) override final;
};
