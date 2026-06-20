#include "Network/SWGSocketWriter.h"

FSWGSocketWriter::FSWGSocketWriter(FSocket* InSocket, FSWGSession* InSession)
	: Socket(InSocket), Session(InSession), bRunning(true)
{
}

FSWGSocketWriter::~FSWGSocketWriter()
{
}

bool FSWGSocketWriter::Init()
{
	return Socket != nullptr && Session != nullptr;
}

uint32 FSWGSocketWriter::Run()
{
	// TODO: Phase 1 implementation
	// - Dequeue from OutgoingReliable/OutgoingUnreliable
	// - Add sequence numbers (reliable only)
	// - Encrypt/compress
	// - Append CRC
	// - Send via socket
	// - Handle retransmits

	while (bRunning)
	{
		ProcessOutgoingMessages();
		HandleRetransmits();
		FPlatformProcess::SleepNoStats(0.001f);
	}
	return 0;
}

void FSWGSocketWriter::Stop()
{
	bRunning = false;
}

void FSWGSocketWriter::ProcessOutgoingMessages()
{
	// TODO: Dequeue and transmit
}

void FSWGSocketWriter::HandleRetransmits()
{
	// TODO: Resend unacked packets
}

void FSWGSocketWriter::SendSessionRequest(uint32 CRCSeed, uint32 ConnectionID)
{
	// TODO: Build and send SessionRequest (0x0001)
}

void FSWGSocketWriter::SendPacket(const FSWGPacket& Packet, bool bReliable)
{
	// TODO: Prepare packet for transmission
}

void FSWGSocketWriter::SendPing()
{
	// TODO: Send Ping (0x0600)
}

void FSWGSocketWriter::SendDisconnect()
{
	// TODO: Send Disconnect (0x0500)
}
