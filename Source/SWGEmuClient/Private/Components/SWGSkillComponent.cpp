#include "Components/SWGSkillComponent.h"
#include "Network/SWGPacket.h"

USWGSkillComponent::USWGSkillComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGSkillComponent::ApplyBase1(FSWGPacket& Packet)
{
	SkillList = ReadBaselineVector<FString>(Packet, [](FSWGPacket& P) { return P.ReadAsciiString(); });
	bHasBase1 = true;
}

void USWGSkillComponent::ApplyBase4(FSWGPacket& Packet)
{
	SkillMods = ReadBaselineMap<FSkillModifier>(Packet, [](FSWGPacket& P)
	{
		FSkillModifier Mod;
		Mod.Deserialize(P);
		return Mod;
	});
	bHasBase4 = true;
}
