#include "Network/Handler/SWGHandshakeComponent.h"
#include "Network/Handler/SWGEncryptionComponent.h"
#include "Network/SWGSessionOp.h"
#include "Network/SWGSession.h"
#include "Network/SWGPacket.h"
#include "EncryptionComponent.h"
#include "HAL/PlatformTime.h"

FSWGHandshakeComponent::FSWGHandshakeComponent(FSWGSession* InSession, FSWGEncryptionComponent* InEncryption)
	: HandlerComponent(FName(TEXT("SWGHandshake")))
	, Session(InSession)
	, Encryption(InEncryption)
{
	bRequiresHandshake = true;
}

void FSWGHandshakeComponent::Initialize()
{
	Initialized();
}

bool FSWGHandshakeComponent::IsValid() const
{
	return Session != nullptr && Encryption != nullptr;
}

void FSWGHandshakeComponent::Incoming(FBitReader& Packet)
{
	const int32 NumBytes = (int32)Packet.GetNumBytes();
	if (NumBytes < 2)
		return;

	const uint8* Data = Packet.GetData();

	if (!SWGIsSessionPacket(Data, NumBytes))
		return;

	const ESWGSessionOp Op = SWGGetSessionOp(Data);

	if (Op == ESWGSessionOp::SessionResponse)
	{
		HandleSessionResponse(Packet);
	}
	else if (Op == ESWGSessionOp::NetStatRequest)
	{
		HandleNetStatRequest(Packet);
	}
	else if (Op == ESWGSessionOp::Disconnect)
	{
		HandleDisconnect(Packet);
	}
}

void FSWGHandshakeComponent::NotifyHandshakeBegin()
{
	SendSessionRequest();
}

int32 FSWGHandshakeComponent::GetReservedPacketBits() const
{
	return 0;
}

void FSWGHandshakeComponent::SendSessionRequest()
{
	if (RequestID == 0)
		RequestID = FMath::Rand();

	// Wire layout:
	//   [0x00][SessionRequest]   op prefix
	//   [0x00][0x02]             CRC seed size = 2 (BE uint16)
	//   [0x00][0x00]             padding
	//   [id0..id3]               connection ID (LE uint32)
	//   [sz3..sz0]               max packet size (BE uint32)
	FSWGPacket Pkt;
	Pkt.WriteByte(0x00);
	Pkt.WriteByte(static_cast<uint8>(ESWGSessionOp::SessionRequest));

	Pkt.WriteByte(0x00); // CRC size hi
	Pkt.WriteByte(0x02); // CRC size lo = 2

	Pkt.WriteByte(0x00); // padding
	Pkt.WriteByte(0x00);

	// Connection ID — little-endian
	const uint32 ConnID = (uint32)RequestID;
	Pkt.WriteByte((uint8)((ConnID >> 0) & 0xFF));
	Pkt.WriteByte((uint8)((ConnID >> 8) & 0xFF));
	Pkt.WriteByte((uint8)((ConnID >> 16) & 0xFF));
	Pkt.WriteByte((uint8)((ConnID >> 24) & 0xFF));

	// Max packet size — big-endian
	const uint32 MaxSize = Session->MaxPacketSize;
	Pkt.WriteByte((uint8)((MaxSize >> 24) & 0xFF));
	Pkt.WriteByte((uint8)((MaxSize >> 16) & 0xFF));
	Pkt.WriteByte((uint8)((MaxSize >> 8) & 0xFF));
	Pkt.WriteByte((uint8)((MaxSize >> 0) & 0xFF));

	Pkt.bEncrypted = false;
	Pkt.bCompressed = false;

	Session->State = ESWGSessionState::Connecting;
	Session->ConnectionID = ConnID;
	Session->OutgoingUnreliable.Enqueue(MoveTemp(Pkt));
}

void FSWGHandshakeComponent::HandleSessionResponse(FBitReader& Packet)
{
	const uint8* Data = Packet.GetData();
	const int32 NumBytes = (int32)Packet.GetNumBytes();

	// Minimum: op(2) + requestID(4) + key(4) + crcLen(1) + useComp(1) + seedSz(1) + udpSize(4) = 17
	if (NumBytes < 17)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGHandshakeComponent: SessionResponse too short (%d bytes)"), NumBytes);
		Packet.SetError();
		return;
	}

	int32 Off = 2; // skip op bytes

	// Request ID — little-endian uint32
	const uint32 ReceivedID =
		((uint32)Data[Off + 0]) |
		((uint32)Data[Off + 1] << 8) |
		((uint32)Data[Off + 2] << 16) |
		((uint32)Data[Off + 3] << 24);
	Off += 4;

	if (ReceivedID != (uint32)RequestID)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGHandshakeComponent: request ID mismatch (got %u expected %u)"),
			ReceivedID, (uint32)RequestID);
	}

	// Encryption key — big-endian uint32
	const uint32 Key =
		((uint32)Data[Off + 0] << 24) |
		((uint32)Data[Off + 1] << 16) |
		((uint32)Data[Off + 2] << 8) |
		((uint32)Data[Off + 3]);
	Off += 4;

	const uint8 CrcLength      = Data[Off++];
	const uint8 UseCompression = Data[Off++];
	const uint8 SeedSize       = Data[Off++];

	// Max UDP packet size — big-endian uint32
	const uint32 UDPPacketSize =
		((uint32)Data[Off + 0] << 24) |
		((uint32)Data[Off + 1] << 16) |
		((uint32)Data[Off + 2] << 8) |
		((uint32)Data[Off + 3]);

	if (UDPPacketSize != 0)
		Session->MaxPacketSize = UDPPacketSize;

	UE_LOG(LogTemp, Log,
		TEXT("FSWGHandshakeComponent: SessionResponse — Key=0x%08X CrcLen=%u Comp=%u SeedSz=%u UDPSize=%u"),
		Key, CrcLength, UseCompression, SeedSize, UDPPacketSize);

	// Set the encryption key on the session and the encryption component.
	Session->EncryptionKey = Key;

	FEncryptionData EncData;
	EncData.Key.SetNumUninitialized(sizeof(Key));
	FMemory::Memcpy(EncData.Key.GetData(), &Key, sizeof(Key));
	Encryption->SetEncryptionData(EncData);
	Encryption->EnableEncryption();

	Session->State = ESWGSessionState::Connected;

	// Signal the pipeline that the handshake is complete.
	Initialized();
}

void FSWGHandshakeComponent::HandleNetStatRequest(FBitReader& Packet)
{
	const uint8* Data = Packet.GetData();
	const int32 NumBytes = (int32)Packet.GetNumBytes();

	// NetStatRequest format: [0x00][0x07][tick(2 BE)][stats...]
	if (NumBytes < 4)
		return;

	const uint16 Tick = ((uint16)Data[2] << 8) | (uint16)Data[3];

	// Respond with NetStatResponse: [0x00][0x08][tick(2 BE)][stats]
	// For now, minimal response: just echo back the tick.
	FSWGPacket Response;
	Response.WriteByte(0x00);
	Response.WriteByte(static_cast<uint8>(ESWGSessionOp::NetStatResponse));
	Response.WriteByte((uint8)(Tick >> 8));
	Response.WriteByte((uint8)(Tick & 0xFF));
	Response.bEncrypted = false;
	Response.bCompressed = false;

	Session->OutgoingUnreliable.Enqueue(MoveTemp(Response));
	Session->LastPingReceived = (uint64)(FPlatformTime::Seconds() * 1000.0);
}

void FSWGHandshakeComponent::HandleDisconnect(FBitReader& Packet)
{
	// Server is closing the connection.
	UE_LOG(LogTemp, Log, TEXT("FSWGHandshakeComponent: received Disconnect from server"));
	Session->State = ESWGSessionState::Disconnected;
}
