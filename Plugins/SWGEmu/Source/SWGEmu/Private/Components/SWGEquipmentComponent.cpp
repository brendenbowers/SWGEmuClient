#include "Components/SWGEquipmentComponent.h"

USWGEquipmentComponent::USWGEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGEquipmentComponent::ApplyBase6(FSWGPacket& Packet)
{
	// TODO: EquipmentList/AlternateAppearance from CREO base6.
}
