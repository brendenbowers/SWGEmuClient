#include "Objects/World/SWGCell.h"
#include "Components/LightComponent.h"
#include "HAL/IConsoleManager.h"

// Live multiplier on every room light, applied whenever a room's lights come
// on — step out and back in to see a new value. POB room lights need a much
// lower intensity in UE's lighting model; keep this multiplier for tuning.
static TAutoConsoleVariable<float> CVarRoomLightScale(
	TEXT("swg.RoomLightScale"), 0.001f,
	TEXT("Multiplier on interior (POB cell) light intensity. Re-enter the room to apply."));

void ASWGCell::SetRoomLightsEnabled(bool bEnabled)
{
	const float Scale = CVarRoomLightScale.GetValueOnGameThread();
	for (int32 LightIndex = 0; LightIndex < RoomLights.Num(); ++LightIndex)
	{
		ULightComponent* Light = RoomLights[LightIndex];
		if (!Light)
		{
			continue;
		}
		if (bEnabled && RoomLightBaseIntensity.IsValidIndex(LightIndex))
		{
			Light->SetIntensity(RoomLightBaseIntensity[LightIndex] * Scale);
		}
		Light->SetVisibility(bEnabled);
	}
}

void ASWGCell::AddRoomLight(ULightComponent* Light)
{
	if (!Light)
	{
		return;
	}
	RoomLights.Add(Light);
	RoomLightBaseIntensity.Add(Light->Intensity);
}
