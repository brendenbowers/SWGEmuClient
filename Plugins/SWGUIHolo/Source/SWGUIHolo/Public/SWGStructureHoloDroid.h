#pragma once

#include "SWGHoloProjectorActor.h"
#include "SWGStructureHoloDroid.generated.h"

/** Reuses the holo projector's training remote for both placement drones. */
UCLASS(NotPlaceable)
class SWGUIHOLO_API ASWGStructureHoloDroid : public ASWGHoloProjectorActor
{
	GENERATED_BODY()
public:
	ASWGStructureHoloDroid();
};
