#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Sockets.h"

class FSWGPacketHandler;

/**
 * FSWGSocketReader — background receive thread.
 *
 * Responsibility: Recv() bytes from the UDP socket and push each datagram
 * into FSWGPacketHandler::Incoming(). All protocol processing (CRC, decrypt,
 * decompress, reliability, handshake) is handled by the pipeline components;
 * the reader thread knows nothing about the SOE protocol.
 */
class SWGEMU_API FSWGSocketReader : public FRunnable
{
public:
	FSWGSocketReader(FSocket* InSocket, FSWGPacketHandler* InHandler);
	virtual ~FSWGSocketReader() override;

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override {}

private:
	FSocket*            Socket;
	FSWGPacketHandler*  Handler;
	volatile bool       bRunning;
};
