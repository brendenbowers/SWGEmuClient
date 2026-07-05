#include "Components/SWGTangibleComponent.h"

USWGTangibleComponent::USWGTangibleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGTangibleComponent::ApplyBase3(FSWGPacket& Packet)
{
	// TODO: port SWGTangibleBaselineParser::ParseBase3 field-for-field once
	// the free-function dispatch (SWGTangibleBaselineApply::ApplyBase3) is wired up.
}
