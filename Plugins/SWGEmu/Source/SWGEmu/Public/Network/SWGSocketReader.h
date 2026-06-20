#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Sockets.h"
#include "SWGSession.h"

/**
 * FSWGSocketReader is a background thread that receives UDP packets from the server.
 *
 * Responsibilities:
 * - Poll the socket for incoming data
 * - Decrypt and decompress packets
 * - Reassemble fragmented messages
 * - Feed reassembled packets to Session.IncomingMessages queue
 *
 */
class FSWGSocketReader : public FRunnable
{
public:
	/**
	 * Constructor.
	 *
	 * @param InSocket   Pointer to the UDP socket (not owned by this reader)
	 * @param InSession  Pointer to the session state (not owned)
	 */
	FSWGSocketReader(FSocket* InSocket, FSWGSession* InSession);

	virtual ~FSWGSocketReader();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override { }

private:
	FSocket* Socket;
	FSWGSession* Session;
	volatile bool bRunning;

	/**
	 * Process a single received packet:
	 * - Decrypt if encrypted
	 * - Decompress if compressed
	 * - Reassemble fragments
	 * - Queue complete messages
	 */
	void ProcessIncomingPacket(FSWGPacket& Packet);

	/**
	 * Parse the session layer op code and route accordingly.
	 * Handles DataChannel, DataAck, Ping, Disconnect, etc.
	 */
	void HandleSessionOp(FSWGPacket& Packet);

	/**
	 * Handle DataAck: remove ACK'd packets from the reliability window.
	 */
	void HandleDataAck(FSWGPacket& Packet);

	/**
	 * Handle a multi-packet (0x0300): unbundle sub-packets and process each.
	 */
	void HandleMultiPacket(FSWGPacket& Packet);

	/**
	 * Handle a fragment (0x000D-0x0010): accumulate into FragBuffer, queue when complete.
	 */
	void HandleDataFrag(FSWGPacket& Packet);
};
