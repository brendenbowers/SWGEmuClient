#include "Components/SWGExperienceComponent.h"

USWGExperienceComponent::USWGExperienceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

int32 USWGExperienceComponent::FindExperience(const FString& Type) const
{
	const FExperience* Found = ExperienceList.Items.FindByPredicate([&Type](const FExperience& Item)
	{
		return Item.Type == Type;
	});
	return Found ? Found->Value : 0;
}

void USWGExperienceComponent::ApplyBase8(const FPlayerObjectBaseline& Baseline)
{
	ExperienceList = Baseline.ExperienceList;
	bHasBase8 = true;
}

void USWGExperienceComponent::ApplyDelta8(const FPlayerObjectDelta& Delta)
{
	ApplyKeyedListChanges(Delta.ExperienceList, ExperienceList, [](const FExperience& A, const FExperience& B)
	{
		return A.Type == B.Type;
	});
}
