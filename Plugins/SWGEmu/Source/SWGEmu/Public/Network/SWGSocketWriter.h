#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Sockets.h"
#include "SWGSession.h"

/**
 * FSWGSocketWriter is a background thread that transmits UDP packets to the server.
 *
 * Responsibilities:
 * - Dequeue messages from Session.OutgoingReliable / OutgoingUnreliable
 * - Add sequence numbers (for reliable messages)
 * - Encrypt and optionally compress
 * - Compute and append CRC
 * - Send via the UDP socket
 * - Track sent packets in the reliability window
 * - Resend unacknowledged packets
 *
 */
class FSWGSocketWriter : public FRunnable
{
public:
	/**
	 * Constructor.
	 *
	 * @param InSocket   Pointer to the UDP socket (not owned by this writer)
	 * @param InSession  Pointer to the session state (not owned)
	 */
	FSWGSocketWriter(FSocket* InSocket, FSWGSession* InSession);

	virtual ~FSWGSocketWriter();

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
	 * Check outgoing message queues and transmit them.
	 */
	void ProcessOutgoingMessages();

	/**
	 * Check for unacknowledged packets that need retransmission.
	 */
	void HandleRetransmits();

	/**
	 * Build and send a SessionRequest packet (initiates connection).
	 */
	void SendSessionRequest(uint32 CRCSeed, uint32 ConnectionID);

	/**
	 * Prepare a packet for transmission:
	 * - Prepend session op code
	 * - Optionally compress payload
	 * - Encrypt (starting after op code)
	 * - Compute and append CRC
	 * - Send via socket
	 */
	void SendPacket(const FSWGPacket& Packet, bool bReliable = true);

	/**
	 * Send a ping (0x0600) to keep the connection alive.
	 */
	void SendPing();

	/**
	 * Send a disconnect (0x0500) to cleanly close the session.
	 */
	void SendDisconnect();
};
