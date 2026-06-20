#pragma once

#include "CoreMinimal.h"
#include "PacketHandler.h"

struct FSWGSession;

/**
 * FSWGCrcComponent — SOE CRC stage of the PacketHandler pipeline.
 *
 * Incoming: verify the trailing 2-byte CRC (seed = session encryption key); drop on mismatch.
 * Outgoing: append the 2-byte CRC.
 *
 */
class FSWGCrcComponent : public HandlerComponent
{
public:
	/** @param InSession  Back-pointer to session state (CRC seed = EncryptionKey). Not owned. */
	explicit FSWGCrcComponent(FSWGSession* InSession);

	// HandlerComponent interface
	virtual void Initialize() override;
	virtual bool IsValid() const override;
	virtual void Incoming(FBitReader& Packet) override;
	virtual void Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits) override;
	virtual int32 GetReservedPacketBits() const override;

private:
	FSWGSession* Session;
};
