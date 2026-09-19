#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Components/LightComponent.h"

/**
 * Retail lit a room only by the lights authored in its POB cell — never by
 * the sun or the sky. Lighting channel 0 stays the world (sun, sky light,
 * everything outdoors); channel 1 is interiors: room geometry, interior
 * props and each room's own lights (ASWGCell::RoomLights). Characters sit
 * on both, so they take the sun outside and the room's lights inside.
 */
FORCEINLINE void SWGSetInteriorLightingChannel(UPrimitiveComponent& Component, bool bAlsoWorld)
{
	Component.SetLightingChannels(/*bChannel0*/ bAlsoWorld, /*bChannel1*/ true, /*bChannel2*/ false);
}

FORCEINLINE void SWGSetInteriorLightingChannel(ULightComponent& Light)
{
	Light.SetLightingChannels(/*bChannel0*/ false, /*bChannel1*/ true, /*bChannel2*/ false);
}
