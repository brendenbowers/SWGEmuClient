#pragma once

#include "CoreMinimal.h"
#include "PacketHandler.h"

struct FSWGSession;
class FSWGEncryptionComponent;

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
	/**
	 * @param InSession     Back-pointer to session state. Not owned.
	 * @param InEncryption  Encryption stage to key + enable on SessionResponse. Not owned.
	 */
	FSWGHandshakeComponent(FSWGSession* InSession, FSWGEncryptionComponent* InEncryption);

	// HandlerComponent interface
	virtual void Initialize() override;
	virtual bool IsValid() const override;
	virtual void Incoming(FBitReader& Packet) override;
	virtual void NotifyHandshakeBegin() override;
	virtual int32 GetReservedPacketBits() const override;

private:
	FSWGSession* Session;
	FSWGEncryptionComponent* Encryption;

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
