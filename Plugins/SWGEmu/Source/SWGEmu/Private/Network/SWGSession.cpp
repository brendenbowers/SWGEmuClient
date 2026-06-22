#include "Network/SWGSession.h"

void FSWGSession::Reset()
{
	EncryptionKey    = 0;
	MaxPacketSize    = 496;
	WindowResendSize = 32;
	State            = ESWGSessionState::Disconnected;

	LastPingReceived   = 0;
	LastPacketReceived = 0;
	LastPacketSent     = 0;

	// Drain queues to release any buffered packets.
	FSWGPacket Discard;
	while (OutgoingReliable.Dequeue(Discard))   {}
	while (OutgoingUnreliable.Dequeue(Discard)) {}
	while (IncomingMessages.Dequeue(Discard))   {}
}
