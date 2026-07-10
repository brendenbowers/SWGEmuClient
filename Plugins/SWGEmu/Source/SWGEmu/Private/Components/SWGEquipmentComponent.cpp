#include "Components/SWGEquipmentComponent.h"
#include "Network/SWGPacket.h"

USWGEquipmentComponent::USWGEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGEquipmentComponent::ApplyBase6(FSWGPacket& Packet)
{
	EquipmentList = ReadBaselineVector<FEquiptmentItem>(Packet, [](FSWGPacket& P)
	{
		FEquiptmentItem Item;
		Item.Deserialize(P);
		return Item;
	});

	AlternateAppearance = Packet.ReadAsciiString();
	bHasBase6 = true;
}
