#include "Components/SWGSpaceMissionComponent.h"

USWGSpaceMissionComponent::USWGSpaceMissionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGSpaceMissionComponent::ApplyBase4(const FCreatureObjectBaseline& Baseline)
{
	ListenId = Baseline.ListenId;
	SpaceMissionObjects = Baseline.SpaceMissionObjects;
	bHasBase4 = true;
}

void USWGSpaceMissionComponent::ApplyDelta4(const FCreatureObjectDelta& Delta)
{
	if (Delta.ListenId.IsSet()) { ListenId = *Delta.ListenId; }

	ApplyKeyedListChanges(Delta.SpaceMissionObjects, SpaceMissionObjects,
		[](const FGroupMissionCriticalObject& A, const FGroupMissionCriticalObject& B)
		{
			return A.MissionOwnerId == B.MissionOwnerId;
		});
}
