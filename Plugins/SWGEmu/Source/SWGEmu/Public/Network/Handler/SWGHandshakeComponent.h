#pragma once

#include "CoreMinimal.h"
#include "PacketHandler.h"

struct FSWGSession;
class FSWGEncryptionComponent;
class FSWGPacketHandler;

/**
 * FSWGHandshakeComponent — SOE session handshake stage.
 *
 * Sets bRequiresHandshake so the pipeline waits for it before sending game traffic.
 * NotifyHandshakeBegin sends SessionRequest; Incoming parses SessionResponse, sets the
 * key on the encryption component + enables encryption, then fires HandshakeComplete.
 */
class FSWGHandshakeComponent : public HandlerComponent
{
public:
	static FString GetComponentName();

	/**
	 * @param InSession     Back-pointer to session state. Not owned.
	 * @param InHandler     Back-pointer to packet handler for OnSessionInitialized callback. Not owned.
	 */
	FSWGHandshakeComponent(TWeakPtr<FSWGSession> InSession, FSWGPacketHandler* InHandler);

	// HandlerComponent interface
	virtual void Initialize() override;
	virtual bool IsValid() const override;
	virtual void Incoming(FBitReader& Packet) override;
	virtual void NotifyHandshakeBegin() override;
	virtual int32 GetReservedPacketBits() const override;
	virtual void Tick(float DeltaTime) override;

private:
	TWeakPtr<FSWGSession> Session;
	FSWGPacketHandler* PacketHandler = nullptr;

	/** Random connection ID sent in SessionRequest, validated against SessionResponse. */
	int32 RequestID = 0;

	/**
	 * Core3's BaseClient::checkNetStatus() disconnects a session ("netStatusTimeout
	 * on client") if it never receives a NetStatusRequestMessage from the CLIENT —
	 * the protocol is client-initiated, not server-initiated: BaseClient::connect()
	 * (an outgoing, inter-server connection) is the only place NETSTATUSREQUEST_TIME
	 * (5000ms) is scheduled, and BaseClient::handleNetStatusRequest() is purely a
	 * receive-and-reply handler. Game client sessions never get that server-side
	 * request task scheduled at all — confirmed empirically: opcode 0x0700 never
	 * once appeared in ~15,000 received packets across every session logged today.
	 * So this side must periodically SEND NetStatusRequestMessage on its own
	 * initiative; the server's handleNetStatusRequest() replies with
	 * NetStatusResponseMessage AND resets its own disconnect timer right there —
	 * that reset is what actually keeps the session alive.
	 */
	double SecondsSinceLastNetStatusRequest = 0.0;
	static constexpr double NetStatusRequestIntervalSeconds = 4.0; // < Core3's 5s NETSTATUSREQUEST_TIME, for margin

	/** Build and send the SOE SessionRequest. */
	void SendSessionRequest();

	/** Parse SessionResponse: key + MaxPacketSize, enable encryption, complete handshake. */
	void HandleSessionResponse(FBitReader& Packet);

	/** Handle incoming NetStatRequest: respond with NetStatResponse. */
	void HandleNetStatRequest(FBitReader& Packet);

	/** Handle incoming NetStatResponse: the server's reply to our own NetStatRequest. */
	void HandleNetStatResponse(FBitReader& Packet);

	/** Handle incoming Disconnect: mark session as disconnected. */
	void HandleDisconnect(FBitReader& Packet);

	/** Send NetStatusRequestMessage to the server — see SecondsSinceLastNetStatusRequest's comment. */
	void SendNetStatusRequest();
};
