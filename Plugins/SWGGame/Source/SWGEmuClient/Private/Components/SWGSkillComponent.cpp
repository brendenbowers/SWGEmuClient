#include "Components/SWGSkillComponent.h"

USWGSkillComponent::USWGSkillComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGSkillComponent::ApplyBase1(const FCreatureObjectBaseline& Baseline)
{
	SkillList = Baseline.SkillList;
	bHasBase1 = true;
}

void USWGSkillComponent::ApplyBase4(const FCreatureObjectBaseline& Baseline)
{
	SkillMods = Baseline.SkillMods;
	bHasBase4 = true;
}

void USWGSkillComponent::ApplyDelta1(const FCreatureObjectDelta& Delta)
{
	ApplyIndexedListChanges(Delta.SkillList, SkillList);
}

void USWGSkillComponent::ApplyDelta4(const FCreatureObjectDelta& Delta)
{
	ApplyKeyedListChanges(Delta.SkillMods, SkillMods, [](const FSkillModifier& A, const FSkillModifier& B)
	{
		return A.SkillModString == B.SkillModString;
	});
}

void USWGSkillComponent::ApplyBase9(const FPlayerObjectBaseline& Baseline)
{
	AbilityList = Baseline.AbilityList;
	bHasPlayerBase9 = true;
}

void USWGSkillComponent::ApplyDelta9(const FPlayerObjectDelta& Delta)
{
	ApplyIndexedListChanges(Delta.AbilityList, AbilityList);
}
