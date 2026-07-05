#include "Components/SWGSkillComponent.h"

USWGSkillComponent::USWGSkillComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGSkillComponent::ApplyBase1(FSWGPacket& Packet)
{
	// TODO: SkillList from CREO base1.
}

void USWGSkillComponent::ApplyBase4(FSWGPacket& Packet)
{
	// TODO: SkillMods from CREO base4.
}
