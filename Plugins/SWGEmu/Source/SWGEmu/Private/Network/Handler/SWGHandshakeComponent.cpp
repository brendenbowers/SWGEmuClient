#include "Network/Handler/SWGHandshakeComponent.h"
#include "Network/Handler/SWGEncryptionComponent.h"
#include "Network/Handler/SWGPacketHandler.h"
#include "Network/SWGSessionOp.h"
#include "Network/SWGSession.h"
#include "Network/SWGPacket.h"
#include "EncryptionComponent.h"
#include "HAL/PlatformTime.h"

FString FSWGHandshakeComponent::GetComponentName()
{
	static FString Name = TEXT("SWGHandshake");
	return Name;
}

FSWGHandshakeComponent::FSWGHandshakeComponent(TWeakPtr<FSWGSession> InSession, FSWGPacketHandler* InHandler)
	: HandlerComponent(*GetComponentName())
	, Session(InSession)
	, PacketHandler(InHandler)
{
	bRequiresHandshake = true;
}

void FSWGHandshakeComponent::Initialize()
{
	if (Handler)
		Initialized();
}

bool FSWGHandshakeComponent::IsValid() const
{
	return Session != nullptr;
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
	else if (Op == ESWGSessionOp::NetStatResponse)
	{
		HandleNetStatResponse(Packet);
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

	TSharedPtr<FSWGSession> SessionPtr = Session.Pin();
	if (!SessionPtr.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGHandshakeComponent: Session pointer is invalid"));
		return;
	}

	// Max packet size — big-endian
	const uint32 MaxSize = SessionPtr->MaxPacketSize;
	Pkt.WriteByte((uint8)((MaxSize >> 24) & 0xFF));
	Pkt.WriteByte((uint8)((MaxSize >> 16) & 0xFF));
	Pkt.WriteByte((uint8)((MaxSize >> 8) & 0xFF));
	Pkt.WriteByte((uint8)((MaxSize >> 0) & 0xFF));

	Pkt.bEncrypted = false;
	Pkt.bCompressed = false;

	SessionPtr->State = ESWGSessionState::Connecting;
	SessionPtr->ConnectionID = ConnID;
	SessionPtr->OutgoingUnreliable.Enqueue(MoveTemp(Pkt));
}

void FSWGHandshakeComponent::HandleSessionResponse(FBitReader& Packet)
{
	TSharedPtr<FSWGSession> SessionPtr = Session.Pin();
	if (!SessionPtr.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGHandshakeComponent: Session pointer is invalid"));
		return;
	}

	if (!PacketHandler)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGHandshakeComponent: PacketHandler pointer is invalid"));
		return;
	}

	const uint8* PacketData = Packet.GetData();
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
		((uint32)PacketData[Off + 0]) |
		((uint32)PacketData[Off + 1] << 8) |
		((uint32)PacketData[Off + 2] << 16) |
		((uint32)PacketData[Off + 3] << 24);
	Off += 4;

	if (ReceivedID != (uint32)RequestID)
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGHandshakeComponent: request ID mismatch (got %u expected %u)"),
			ReceivedID, (uint32)RequestID);
	}

	// Encryption key — big-endian uint32
	const uint32 Key =
		((uint32)PacketData[Off + 0] << 24) |
		((uint32)PacketData[Off + 1] << 16) |
		((uint32)PacketData[Off + 2] << 8) |
		((uint32)PacketData[Off + 3]);
	Off += 4;

	const uint8 CrcLength      = PacketData[Off++];
	const uint8 UseCompression = PacketData[Off++];
	const uint8 SeedSize       = PacketData[Off++];

	// Max UDP packet size — big-endian uint32
	const uint32 UDPPacketSize =
		((uint32)PacketData[Off + 0] << 24) |
		((uint32)PacketData[Off + 1] << 16) |
		((uint32)PacketData[Off + 2] << 8) |
		((uint32)PacketData[Off + 3]);

	if (UDPPacketSize != 0)
		SessionPtr->MaxPacketSize = UDPPacketSize;

	UE_LOG(LogTemp, Log,
		TEXT("FSWGHandshakeComponent: SessionResponse — Key=0x%08X CrcLen=%u Comp=%u SeedSz=%u UDPSize=%u"),
		Key, CrcLength, UseCompression, SeedSize, UDPPacketSize);

	SessionPtr->State = ESWGSessionState::Connected;

	SessionData SessionDataToInit;
	SessionDataToInit.EncryptionKey = Key;
	SessionDataToInit.UseCompression = UseCompression;
	SessionDataToInit.MaxPacketSize = UDPPacketSize;
	PacketHandler->OnSessionInitialized(SessionDataToInit);

	// Signal the pipeline that the handshake is complete.
	if (Handler)
	{
		Initialized();
	}
}

void FSWGHandshakeComponent::HandleNetStatRequest(FBitReader& Packet)
{
	TSharedPtr<FSWGSession> SessionPtr = Session.Pin();
	if (!SessionPtr.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGHandshakeComponent: Session pointer is invalid"));
		return;
	}

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

	SessionPtr->OutgoingUnreliable.Enqueue(MoveTemp(Response));
	SessionPtr->LastPingReceived = (uint64)(FPlatformTime::Seconds() * 1000.0);
	UE_LOG(LogTemp, Log, TEXT("FSWGHandshakeComponent: received NetStatRequest tick=%u — queued NetStatResponse"), Tick);
}

void FSWGHandshakeComponent::HandleNetStatResponse(FBitReader& Packet)
{
	const uint8* Data = Packet.GetData();
	const int32 NumBytes = (int32)Packet.GetNumBytes();
	if (NumBytes < 4)
		return;

	const uint16 Tick = ((uint16)Data[2] << 8) | (uint16)Data[3];
	UE_LOG(LogTemp, Verbose, TEXT("FSWGHandshakeComponent: received NetStatResponse tick=%u"), Tick);
}

void FSWGHandshakeComponent::Tick(float DeltaTime)
{
	TSharedPtr<FSWGSession> SessionPtr = Session.Pin();
	if (!SessionPtr.IsValid() || SessionPtr->State != ESWGSessionState::Connected)
		return;

	SecondsSinceLastNetStatusRequest += (double)DeltaTime;
	if (SecondsSinceLastNetStatusRequest >= NetStatusRequestIntervalSeconds)
	{
		SecondsSinceLastNetStatusRequest = 0.0;
		SendNetStatusRequest();
	}
}

void FSWGHandshakeComponent::SendNetStatusRequest()
{
	TSharedPtr<FSWGSession> SessionPtr = Session.Pin();
	if (!SessionPtr.IsValid())
		return;

	// Wire layout: [0x00][NetStatRequest][tick(2 BE)][5 zero ints][2 zero longs].
	// Confirmed necessary the hard way: a 4-byte tick-only packet (matching
	// NetStatusRequestMessage.h's minimal BasePacket(7) constructor) reached the
	// server fine, but BaseClient::handleNetStatusRequest unconditionally parses
	// 5 ints + 2 longs of stats AFTER the tick and throws
	// StreamIndexOutOfBoundsException when they aren't there — so the header's
	// own constructor doesn't match what the live handler actually requires.
	// This mirrors NetStatusResponseMessage's real 40-byte total (2 op + 2 tick +
	// 36 stat bytes) exactly, just with zeros for stats we don't track.
	const uint16 Tick = (uint16)((uint64)(FPlatformTime::Seconds() * 1000.0) & 0xFFFF);

	FSWGPacket Request;
	Request.WriteByte(0x00);
	Request.WriteByte(static_cast<uint8>(ESWGSessionOp::NetStatRequest));
	Request.WriteByte((uint8)(Tick >> 8));
	Request.WriteByte((uint8)(Tick & 0xFF));
	for (int32 i = 0; i < 5; ++i)
	{
		Request.WriteByte(0); Request.WriteByte(0); Request.WriteByte(0); Request.WriteByte(0);
	}
	for (int32 i = 0; i < 2; ++i)
	{
		for (int32 b = 0; b < 8; ++b)
			Request.WriteByte(0);
	}
	Request.bEncrypted = false;
	Request.bCompressed = false;

	SessionPtr->OutgoingUnreliable.Enqueue(MoveTemp(Request));

	UE_LOG(LogTemp, Log, TEXT("FSWGHandshakeComponent: sent NetStatusRequest tick=%u"), Tick);
}

void FSWGHandshakeComponent::HandleDisconnect(FBitReader& Packet)
{
	TSharedPtr<FSWGSession> SessionPtr = Session.Pin();
	if (!SessionPtr.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGHandshakeComponent: Session pointer is invalid"));
		return;
	}

	// Server is closing the connection.
	UE_LOG(LogTemp, Log, TEXT("FSWGHandshakeComponent: received Disconnect from server"));
	SessionPtr->State = ESWGSessionState::Disconnected;
}
