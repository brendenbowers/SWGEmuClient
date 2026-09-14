#pragma once

#include "CoreMinimal.h"
#include "Subsystems/SWGBaselineHandlerRegistry.h"

/**
 * Handles PLAY baselines. The player object never spawns an actor of its own —
 * the object graph routes its messages to the local player's creature actor,
 * where these components live.
 */
class SWGEMUCLIENT_API FSWGPlayerBaselineHandler final : public ISWGBaselineHandler
{
public:
	bool CanHandleBaseline(const AActor& Actor, const FBaselinesMessage& Msg) const override final;
	bool HandleBaseline(AActor& Actor, const FBaselinesMessage& Msg, const FSWGBaselineArguments& Args) override final;
};
