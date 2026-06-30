#pragma once

#include "CoreMinimal.h"
#include "Flow/SWGFlowState.h"

/**
 * Payload passed when transitioning to GalaxySelected state.
 * Carries the galaxy ID that was selected by the player.
 */
struct FSWGGalaxySelectedPayload : public FSWGTransitionPayload
{
	int32 GalaxyID;

	explicit FSWGGalaxySelectedPayload(int32 InGalaxyID)
		: GalaxyID(InGalaxyID) {}
};
