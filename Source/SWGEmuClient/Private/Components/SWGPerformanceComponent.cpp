#include "Components/SWGPerformanceComponent.h"
#include "Network/SWGPacket.h"

USWGPerformanceComponent::USWGPerformanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USWGPerformanceComponent::ApplyBase6Part1(FSWGPacket& Packet)
{
	PerformanceAnimation = Packet.ReadAsciiString();
	MoodString = Packet.ReadAsciiString();
}

void USWGPerformanceComponent::ApplyBase6Part2(FSWGPacket& Packet)
{
	MoodId = Packet.ReadByte();
	PerformanceStartTime = Packet.ReadInt32();
	PerformanceType = Packet.ReadInt32();
	bHasBase6 = true;
}
