#include "Network/SWGSocketReader.h"
#include "Network/Handler/SWGPacketHandler.h"
#include "Serialization/BitReader.h"

FSWGSocketReader::FSWGSocketReader(FSocket* InSocket, FSWGPacketHandler* InHandler)
	: Socket(InSocket)
	, Handler(InHandler)
	, bRunning(true)
{
}

FSWGSocketReader::~FSWGSocketReader()
{
}

bool FSWGSocketReader::Init()
{
	return Socket != nullptr && Handler != nullptr;
}

uint32 FSWGSocketReader::Run()
{
	// Max SOE datagram is 496 bytes; use a generous fixed buffer.
	uint8 RecvBuf[512];

	while (bRunning)
	{
		uint32 PendingSize = 0;
		if (!Socket->HasPendingData(PendingSize))
		{
			FPlatformProcess::SleepNoStats(0.001f);
			continue;
		}

		int32 BytesRead = 0;
		if (!Socket->Recv(RecvBuf, sizeof(RecvBuf), BytesRead) || BytesRead <= 0)
		{
			FPlatformProcess::SleepNoStats(0.001f);
			continue;
		}

		UE_LOG(LogTemp, Log, TEXT("SWG NET RX (%d wire): %s"),
			BytesRead, *BytesToHex(RecvBuf, BytesRead));

		// Wrap raw bytes in a bit-reader and push through the pipeline.
		// Components (CRC → Decrypt → Decompress → Reliability → Handshake)
		// do all protocol processing in-order.
		FBitReader Reader(RecvBuf, (int64)BytesRead * 8);
		Handler->Incoming(Reader);
	}

	return 0;
}

void FSWGSocketReader::Stop()
{
	bRunning = false;
}
