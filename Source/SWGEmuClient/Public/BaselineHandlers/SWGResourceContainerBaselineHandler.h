#pragma once

#include "CoreMinimal.h"
#include "Subsystems/SWGBaselineHandlerRegistry.h"

/**
 * Handlers RCNO resource containers.
 */
class SWGEMUCLIENT_API FSWGResourceContainerBaselineHandler final : public ISWGBaselineHandler
{
public:
	bool CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const override final;
	bool HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args) override final;
};
