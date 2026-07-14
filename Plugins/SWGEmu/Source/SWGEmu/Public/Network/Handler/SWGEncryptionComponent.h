#pragma once

#include "CoreMinimal.h"
#include "EncryptionComponent.h"

struct FSWGSession;

/**
 * FSWGEncryptionComponent — SOE XOR stream cipher stage.
 *
 * Specializes UE's FEncryptionComponent. Encryption starts disabled; the
 * handshake calls SetEncryptionData() with the server key, then EnableEncryption().
 *
 * Incoming: FSWGCrypto::Decrypt. Outgoing: FSWGCrypto::Encrypt.
 */
class SWGEMU_API FSWGEncryptionComponent : public FEncryptionComponent
{
public:
	static FString GetComponentName();

	/** @param InSession  Back-pointer to session state. Not owned. */
	explicit FSWGEncryptionComponent();

	// FEncryptionComponent interface
	virtual void EnableEncryption() override;
	virtual void DisableEncryption() override;
	virtual bool IsEncryptionEnabled() const override;
	virtual void SetEncryptionData(const FEncryptionData& EncryptionData) override;

	// HandlerComponent interface
	virtual void Initialize() override;
	virtual bool IsValid() const override;
	virtual void Incoming(FBitReader& Packet) override;
	virtual void Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits) override;
	virtual int32 GetReservedPacketBits() const override;

private:
	uint32 EncryptionKey = 0;
	bool bEncryptionEnabled = false;
};
