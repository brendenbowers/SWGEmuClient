#include "Network/Messages/Zone/Object/CommandQueueRemoveIn.h"
#include "Network/SWGPacket.h"

bool FCommandQueueRemoveIn::Parse(FSWGPacket& Packet)
{
	ActionCount = Packet.ReadUInt32();
	Timer       = Packet.ReadFloat();
	Error       = Packet.ReadUInt32();
	ErrorDetail = Packet.ReadUInt32();

	return !Packet.IsError();
}

FString FCommandQueueRemoveIn::DescribeError() const
{
	switch (GetError())
	{
		case ESWGCommandError::None:              return TEXT("ok");
		case ESWGCommandError::InvalidLocomotion: return FString::Printf(TEXT("invalid locomotion (%u)"), ErrorDetail);
		case ESWGCommandError::MissingAbility:    return TEXT("ability not known");
		case ESWGCommandError::InvalidTarget:     return TEXT("invalid target");
		case ESWGCommandError::TooFar:            return TEXT("target out of range");
		case ESWGCommandError::InvalidState:      return FString::Printf(TEXT("invalid state (bit %u)"), ErrorDetail);
		default:                                  return FString::Printf(TEXT("unknown error %u/%u"), Error, ErrorDetail);
	}
}
