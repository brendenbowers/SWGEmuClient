#pragma once

#include "CoreMinimal.h"
#include "Objects/Creature/SWGCreature.h"
#include "SWGPlayer.generated.h"

/**
 * The local player's own CREO. Wire-wise it's spawned from the same SCOT
 * template class as any other creature (see world-object-plan.html
 * "Template FORM types"), so the CRC->actor-class map alone can't tell it
 * apart from an NPC — spawning this subclass for the player specifically
 * (instead of plain ASWGCreature) is a special case the object graph
 * subsystem will need once it knows which ObjectId is "us" (from character
 * select / zone-in), not yet wired up.
 *
 * Exists as a distinct place for PLAY-layer state (profile, quests,
 * abilities, vitals, presence — deferred per the "moveable player with
 * health" milestone scope) to attach later as components, without every NPC
 * ASWGCreature carrying player-only data it'll never use.
 */
UCLASS()
class SWGEMU_API ASWGPlayer : public ASWGCreature
{
	GENERATED_BODY()

public:
	ASWGPlayer(const FObjectInitializer& ObjectInitializer);
};
