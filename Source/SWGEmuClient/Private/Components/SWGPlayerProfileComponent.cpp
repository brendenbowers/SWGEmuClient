#include "Components/SWGPlayerProfileComponent.h"

USWGPlayerProfileComponent::USWGPlayerProfileComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

USWGPlayerProfileComponent* USWGPlayerProfileComponent::FindOrAdd(AActor& Actor)
{
	if (USWGPlayerProfileComponent* Existing = Actor.GetComponentByClass<USWGPlayerProfileComponent>())
	{
		return Existing;
	}

	USWGPlayerProfileComponent* Component = NewObject<USWGPlayerProfileComponent>(&Actor);
	Component->RegisterComponent();
	return Component;
}

void USWGPlayerProfileComponent::ApplyBase3(const FPlayerObjectBaseline& Baseline)
{
	Title = Baseline.Title;
	BirthDate = Baseline.BirthDate;
	TotalPlayedTime = Baseline.TotalPlayedTime;
	Status = Baseline.Status;
	PlayerBitmasks = Baseline.PlayerBitmasks;
	bHasBase3 = true;
}

void USWGPlayerProfileComponent::ApplyBase6(const FPlayerObjectBaseline& Baseline)
{
	PrivilegeFlag = Baseline.PrivilegeFlag;
	bHasBase6 = true;
}

void USWGPlayerProfileComponent::ApplyDelta3(const FPlayerObjectDelta& Delta)
{
	if (Delta.Title.IsSet())           { Title = *Delta.Title; }
	if (Delta.BirthDate.IsSet())       { BirthDate = *Delta.BirthDate; }
	if (Delta.TotalPlayedTime.IsSet()) { TotalPlayedTime = *Delta.TotalPlayedTime; }
	if (Delta.PlayerBitmasks.IsSet())  { PlayerBitmasks = *Delta.PlayerBitmasks; }
}

void USWGPlayerProfileComponent::ApplyDelta6(const FPlayerObjectDelta& Delta)
{
	if (Delta.PrivilegeFlag.IsSet()) { PrivilegeFlag = *Delta.PrivilegeFlag; }
}
