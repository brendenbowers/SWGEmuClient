#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SWGClickForwarder.generated.h"

/** Per-row click target; UButton's dynamic delegate does not carry the sender. Shared by the window and holo UIs. */
UCLASS()
class SWGUICOMMON_API USWGTravelPlanetClickForwarder : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void()> Action;
	UFUNCTION() void HandleClicked() { if (Action) { Action(); } }
	TFunction<void()> HoverAction;
	UFUNCTION() void HandleHovered() { if (HoverAction) { HoverAction(); } }
};
