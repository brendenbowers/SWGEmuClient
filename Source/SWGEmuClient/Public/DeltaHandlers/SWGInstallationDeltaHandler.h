#pragma once

#include "CoreMinimal.h"
#include "Subsystems/SWGDeltaHandlerRegistry.h"

/** Handles INSO deltas. */
class SWGEMUCLIENT_API FSWGInstallationDeltaHandler final : public ISWGDeltaHandler
{
public:
	bool CanHandleDelta(const AActor& Actor, const FDeltasMessage& Msg) const override final;
	bool HandleDelta(AActor& Actor, const FDeltasMessage& Msg, const FSWGDeltaArguments& Args) override final;
};
