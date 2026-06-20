#pragma once

#include "CoreMinimal.h"
#include "PacketHandler.h"

struct FSWGSession;

/**
 * FSWGCompressionComponent — SOE zlib compression stage.
 *
 * Incoming: if the compression flag byte is set, FSWGCrypto::Decompress.
 * Outgoing: FSWGCrypto::Compress and set/clear the flag byte.
 *
 */
class FSWGCompressionComponent : public HandlerComponent
{
public:
	explicit FSWGCompressionComponent(FSWGSession* InSession);

	// HandlerComponent interface
	virtual void Initialize() override;
	virtual bool IsValid() const override;
	virtual void Incoming(FBitReader& Packet) override;
	virtual void Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits) override;
	virtual int32 GetReservedPacketBits() const override;

private:
	FSWGSession* Session;
};
