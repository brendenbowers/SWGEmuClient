#pragma once

#include "CoreMinimal.h"
#include "Subsystems/SWGBaselineHandlerRegistry.h"

/**
 * Handles SCLT baselines
 */
class SWGEMUCLIENT_API FSWGCellBaselineHandler final : public ISWGBaselineHandler
{
public:
	bool CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const override final;
	bool HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args) override final;
};
