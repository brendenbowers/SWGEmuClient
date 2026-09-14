#include "Components/SWGForceComponent.h"

USWGForceComponent::USWGForceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGForceComponent::ApplyBase8(const FPlayerObjectBaseline& Baseline)
{
	ForcePower = Baseline.ForcePower;
	ForcePowerMax = Baseline.ForcePowerMax;
	bHasBase8 = true;
}

void USWGForceComponent::ApplyBase9(const FPlayerObjectBaseline& Baseline)
{
	JediState = Baseline.JediState;
	bHasBase9 = true;
}

void USWGForceComponent::ApplyDelta8(const FPlayerObjectDelta& Delta)
{
	if (Delta.ForcePower.IsSet())    { ForcePower = *Delta.ForcePower; }
	if (Delta.ForcePowerMax.IsSet()) { ForcePowerMax = *Delta.ForcePowerMax; }
}

void USWGForceComponent::ApplyDelta9(const FPlayerObjectDelta& Delta)
{
	if (Delta.JediState.IsSet()) { JediState = *Delta.JediState; }
}
