#pragma once

#include "CoreMinimal.h"
#include "Components/SWGHealthComponent.h"

/**
 * Shared HAM-pool display helpers for the widgets that show a creature's
 * health/action/mind bars (the player's condition box and the target box).
 *
 * These live in a header rather than being repeated per widget because the
 * duplicate anonymous-namespace copies collided the moment unity builds put
 * both widget .cpp files in the same blob.
 */
namespace SWGHamPoolDisplay
{
	/** One pool's value out of a CREO HAM/MaxHAM baseline list, or 0 if the list is short (a partially-applied baseline). */
	inline int32 PoolValue(const TSWGBaselineList<int32>& Pools, ESWGHamPool Pool)
	{
		const int32 Index = static_cast<int32>(Pool);
		return Pools.Items.IsValidIndex(Index) ? Pools.Items[Index] : 0;
	}

	inline FText FormatPool(int32 Current, int32 Max)
	{
		return FText::FromString(FString::Printf(TEXT("%d / %d"), Current, Max));
	}
}
