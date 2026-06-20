#include "Network/Handler/SWGCrcComponent.h"
#include "Network/SWGCrypto.h"
#include "Network/SWGSession.h"

FSWGCrcComponent::FSWGCrcComponent(FSWGSession* InSession)
	: HandlerComponent(FName(TEXT("SWGCrc")))
	, Session(InSession)
{
}

void FSWGCrcComponent::Initialize()
{
	Initialized();
}

bool FSWGCrcComponent::IsValid() const
{
	// EncryptionKey == 0 is valid before the handshake (CRC seed of zero is correct for pre-session packets).
	return Session != nullptr;
}

void FSWGCrcComponent::Incoming(FBitReader& Packet)
{
	const int32 NumBytes = Packet.GetNumBytes();

	if (NumBytes < 2)
	{
		Packet.SetError();
		return;
	}

	const uint8* Data = Packet.GetData();
	const int32 DataLen = NumBytes - 2;

	// Trailing 2-byte CRC is big-endian
	const uint16 ReceivedCRC = ((uint16)Data[DataLen] << 8) | (uint16)Data[DataLen + 1];

	const uint32 ComputedCRC = FSWGCrypto::GenerateCRC(Data, DataLen, Session->EncryptionKey);
	const uint16 ExpectedCRC = (uint16)(ComputedCRC & 0xFFFF);

	if (ReceivedCRC != ExpectedCRC)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGCrcComponent: CRC mismatch — dropping packet"));
		Packet.SetError();
		return;
	}
	TArray<uint8> TrimmedData(Packet.GetData(), DataLen);
	Packet.SetData(MoveTemp(TrimmedData), DataLen * 8);
}

void FSWGCrcComponent::Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits)
{
	const int32 NumBytes = (int32)Packet.GetNumBytes();
	const uint8* Data = Packet.GetData();

	const uint32 CRC = FSWGCrypto::GenerateCRC(Data, NumBytes, Session->EncryptionKey);
	const uint16 CRCShort = (uint16)(CRC & 0xFFFF);

	// Append 2 CRC bytes big-endian
	uint8 Hi = (uint8)(CRCShort >> 8);
	uint8 Lo = (uint8)(CRCShort & 0xFF);
	Packet.Serialize(&Hi, 1);
	Packet.Serialize(&Lo, 1);
}

int32 FSWGCrcComponent::GetReservedPacketBits() const
{
	return 16;
}
