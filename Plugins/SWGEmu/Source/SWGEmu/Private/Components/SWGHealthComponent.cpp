#include "Components/SWGHealthComponent.h"

USWGHealthComponent::USWGHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGHealthComponent::ApplyBase1(FSWGPacket& Packet)
{
	// TODO: BaseHAM from CREO base1.
}

void USWGHealthComponent::ApplyBase3(FSWGPacket& Packet)
{
	// TODO: ShockWounds/Wounds from CREO base3 (appended after Tangible3 tail).
}

void USWGHealthComponent::ApplyBase6(FSWGPacket& Packet)
{
	// TODO: HAM/MaxHAM from CREO base6.
}
