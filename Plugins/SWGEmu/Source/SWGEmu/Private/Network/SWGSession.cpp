#include "Network/SWGSession.h"

void FSWGSession::Reset()
{
	EncryptionKey = 0;
	MaxPacketSize = 496;
	WindowResendSize = 32;
	State = ESWGSessionState::Disconnected;
	OutSeqNext = FSWGSeqNum(0);
	InSeqNext  = FSWGSeqNum(0);
	LastSeqAcked = 0;
	FragTotalSize = 0;
	FragCurrentSize = 0;
	FragBuffer.Empty();
	WindowPackets.Empty();
	LastPingReceived = 0;
	LastPacketReceived = 0;
	LastPacketSent = 0;
}
