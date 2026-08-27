#include "Components/SWGGroupComponent.h"

USWGGroupComponent::USWGGroupComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGGroupComponent::ApplyBase6(const FCreatureObjectBaseline& Baseline)
{
	GroupId = Baseline.GroupId;
	GroupInviterId = Baseline.GroupInviterId;
	GroupInviteCounter = Baseline.GroupInviteCounter;
	bHasBase6 = true;
}
