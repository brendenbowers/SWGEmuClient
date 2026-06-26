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

private:
	TWeakPtr<FSWGSession> Session;
	FSWGPacketHandler* PacketHandler = nullptr;

	/** Random connection ID sent in SessionRequest, validated against SessionResponse. */
	int32 RequestID = 0;

	/** Build and send the SOE SessionRequest. */
	void SendSessionRequest();

	/** Parse SessionResponse: key + MaxPacketSize, enable encryption, complete handshake. */
	void HandleSessionResponse(FBitReader& Packet);

	/** Handle incoming NetStatRequest: respond with NetStatResponse. */
	void HandleNetStatRequest(FBitReader& Packet);

	/** Handle incoming Disconnect: mark session as disconnected. */
	void HandleDisconnect(FBitReader& Packet);
};
