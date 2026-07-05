#include "Components/SWGEncumbranceComponent.h"

USWGEncumbranceComponent::USWGEncumbranceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGEncumbranceComponent::ApplyBase4(FSWGPacket& Packet)
{
	// TODO: Encumbrances from CREO base4.
}
