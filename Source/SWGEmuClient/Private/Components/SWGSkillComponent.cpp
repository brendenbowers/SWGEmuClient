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
