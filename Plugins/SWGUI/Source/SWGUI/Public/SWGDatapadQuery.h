#pragma once

#include "CoreMinimal.h"
#include "SWGInventoryQuery.h"

class UGameInstance;

/**
 * Reads the local player's datapad bag out of the object graph's
 * containment, for USWGDatapadWidget. Same entry shape as inventory
 */
namespace SWGDatapadQuery
{
	/** Fills Contents, sorted by name. True if it differs from what was passed in. */
	SWGUI_API bool Gather(UGameInstance* GameInstance, TArray<FSWGInventoryEntry>& Contents);
}
