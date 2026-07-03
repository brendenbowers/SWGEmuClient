


#include "Objects\Creature\SWGCreature.h"
#include "Network/Objects/Zone/Creature/CreatureObjectBaseline.h"

void ASWGCreature::ApplyBaseline(const FCreatureObjectBaseline& Baseline)
{
	Super::ApplyBaseline(Baseline.Tangible);
}
