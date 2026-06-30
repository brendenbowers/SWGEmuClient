#pragma once

#include "CoreMinimal.h"
#include "Flow/SWGFlowState.h"

/**
 * Payload passed when transitioning to CharacterSelected state.
 * Carries the character ID that was selected by the player.
 */
struct FSWGCharacterSelectedPayload : public FSWGTransitionPayload
{
	int64 CharacterID;

	explicit FSWGCharacterSelectedPayload(int64 InCharacterID)
		: CharacterID(InCharacterID) {}
};
