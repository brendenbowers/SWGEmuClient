#include "Network/SWGSocketReader.h"

FSWGSocketReader::FSWGSocketReader(FSocket* InSocket, FSWGSession* InSession)
	: Socket(InSocket), Session(InSession), bRunning(true)
{
}

FSWGSocketReader::~FSWGSocketReader()
{
}

bool FSWGSocketReader::Init()
{
	return Socket != nullptr && Session != nullptr;
}

uint32 FSWGSocketReader::Run()
{
	// TODO: Phase 1 implementation
	// - Poll socket for incoming UDP packets
	// - Decrypt/decompress based on flags
	// - Reassemble fragments and multi-packets
	// - Queue to Session.IncomingMessages

	while (bRunning)
	{
		FPlatformProcess::SleepNoStats(0.001f);
	}
	return 0;
}

void FSWGSocketReader::Stop()
{
	bRunning = false;
}

void FSWGSocketReader::ProcessIncomingPacket(FSWGPacket& Packet)
{
	// TODO: Route by session op code
}

void FSWGSocketReader::HandleSessionOp(FSWGPacket& Packet)
{
	// TODO: Dispatch on op code (DataChannel, DataAck, Ping, etc)
}

void FSWGSocketReader::HandleDataAck(FSWGPacket& Packet)
{
	// TODO: Remove ACK'd packets from reliability window
}

void FSWGSocketReader::HandleMultiPacket(FSWGPacket& Packet)
{
	// TODO: Unbundle sub-packets
}

void FSWGSocketReader::HandleDataFrag(FSWGPacket& Packet)
{
	// TODO: Accumulate fragments
}
