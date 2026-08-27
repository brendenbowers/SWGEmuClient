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

void USWGGroupComponent::ApplyDelta6(const FCreatureObjectDelta& Delta)
{
	if (Delta.GroupId.IsSet())            { GroupId = *Delta.GroupId; }
	if (Delta.GroupInviterId.IsSet())     { GroupInviterId = *Delta.GroupInviterId; }
	if (Delta.GroupInviteCounter.IsSet()) { GroupInviteCounter = *Delta.GroupInviteCounter; }
}
