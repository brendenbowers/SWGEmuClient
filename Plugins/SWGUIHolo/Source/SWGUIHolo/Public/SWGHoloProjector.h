#pragma once

#include "CoreMinimal.h"

class APawn;

namespace SWGHoloProjector
{
	/** In front of the pawn at Distance (Side to its right), Height above the ground found there: where a hologram projects from. */
	SWGUIHOLO_API bool FindLocation(APawn* Pawn, float Distance, float Height, FVector& OutLocation, float Side = 0.f);
}
