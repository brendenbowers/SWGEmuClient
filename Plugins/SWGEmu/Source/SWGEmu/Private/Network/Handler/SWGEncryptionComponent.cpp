#include "Network/Handler/SWGEncryptionComponent.h"
#include "Network/SWGSessionOp.h"
#include "Network/SWGCrypto.h"
#include "Network/SWGSession.h"

FSWGEncryptionComponent::FSWGEncryptionComponent(FSWGSession* InSession)
	: FEncryptionComponent(FName(TEXT("SWGEncryption")))
	, Session(InSession)
{
}

void FSWGEncryptionComponent::EnableEncryption()
{
	bEncryptionEnabled = true;
}

void FSWGEncryptionComponent::DisableEncryption()
{
	bEncryptionEnabled = false;
}

bool FSWGEncryptionComponent::IsEncryptionEnabled() const
{
	return bEncryptionEnabled;
}

void FSWGEncryptionComponent::SetEncryptionData(const FEncryptionData& EncryptionData)
{
	check(EncryptionData.Key.Num() == sizeof(EncryptionKey));
	FMemory::Memcpy(&EncryptionKey, EncryptionData.Key.GetData(), sizeof(EncryptionKey));
}

void FSWGEncryptionComponent::Initialize()
{
	Initialized();
}

bool FSWGEncryptionComponent::IsValid() const
{
	return Session != nullptr && EncryptionKey != 0;
}

void FSWGEncryptionComponent::Incoming(FBitReader& Packet)
{
	if (!bEncryptionEnabled)
		return;

	const int32 NumBytes = (int32)Packet.GetNumBytes();
	if (NumBytes < 2)
		return;

	uint8* Data = Packet.GetData();
	FSWGCrypto::Decrypt(Data, NumBytes, EncryptionKey, SWGGetPayloadStartOffset(Data));
}

void FSWGEncryptionComponent::Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits)
{
	if (!bEncryptionEnabled)
		return;

	const int32 NumBytes = (int32)Packet.GetNumBytes();
	if (NumBytes < 2)
		return;

	uint8* Data = Packet.GetData();
	FSWGCrypto::Encrypt(Data, NumBytes, EncryptionKey, SWGGetPayloadStartOffset(Data));
}

int32 FSWGEncryptionComponent::GetReservedPacketBits() const
{
	// XOR cipher is in-place — no bytes added.
	return 0;
}
