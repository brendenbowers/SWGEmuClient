#include "Network/Handler/SWGPacketHandler.h"
#include "Network/Handler/SWGCrcComponent.h"
#include "Network/Handler/SWGEncryptionComponent.h"
#include "Network/Handler/SWGCompressionComponent.h"
#include "Network/Handler/SWGReliabilityComponent.h"
#include "Network/Handler/SWGHandshakeComponent.h"
#include "Network/SWGSession.h"

FSWGPacketHandler::FSWGPacketHandler(TWeakPtr<FSWGSession> InSession)
	: SessionPtr(InSession)
{
}

FSWGPacketHandler::~FSWGPacketHandler()
{
	Components.Empty();
}

void FSWGPacketHandler::Initialize()
{
	// Build pipeline in receive order: CRC → Encryption → Compression → Reliability → Handshake.
	// Outgoing runs this list in reverse.

	auto Crc         = MakeShared<FSWGCrcComponent>();
	auto Encryption  = MakeShared<FSWGEncryptionComponent>();
	auto Compression = MakeShared<FSWGCompressionComponent>();
	auto Reliability = MakeShared<FSWGReliabilityComponent>(SessionPtr);
	auto Handshake   = MakeShared<FSWGHandshakeComponent>(SessionPtr, this);

	Components.Reserve(5);
	Components.Empty();
	Components.Add(Crc);
	Components.Add(Encryption);
	Components.Add(Compression);
	Components.Add(Reliability);
	Components.Add(Handshake);

	Handshake->SetActive(true);

	// See FSWGReliabilityComponent::SetUnhandledOpForwarder's comment — sub-packets
	// unwrapped from a MultiPacket that Reliability doesn't itself handle (e.g. a
	// NetStatRequest bundled alongside other traffic) need an explicit path to the
	// Handshake component, since they never pass through FSWGPacketHandler's own
	// per-component loop the way a top-level packet does.
	TWeakPtr<FSWGHandshakeComponent> HandshakeWeak = Handshake;
	Reliability->SetUnhandledOpForwarder([HandshakeWeak](FBitReader& SubPacket)
	{
		if (TSharedPtr<FSWGHandshakeComponent> HandshakePtr = HandshakeWeak.Pin())
		{
			HandshakePtr->Incoming(SubPacket);
		}
	});

	for (TSharedPtr<HandlerComponent>& Comp : Components)
	{
		Comp->Initialize();
	}

	TSharedPtr<FSWGSession> Session = SessionPtr.Pin();
	if (Session.IsValid())
	{
		const int32 ReservedBits = GetTotalReservedPacketBits();
		const int32 MaxPayloadBytes = (int32)Session->MaxPacketSize - (ReservedBits / 8);
		UE_LOG(LogTemp, Log,
			TEXT("FSWGPacketHandler: initialized — reserved %d bits, max payload %d bytes"),
			ReservedBits, MaxPayloadBytes);
	}
}

void FSWGPacketHandler::Incoming(FBitReader& Packet)
{
	for (TSharedPtr<HandlerComponent>& Comp : Components)
	{
		if (Packet.IsError())
			break;

		if (Comp.IsValid() && Comp->IsActive())
			Comp->Incoming(Packet);
	}
}

void FSWGPacketHandler::Outgoing(FBitWriter& Packet, FOutPacketTraits& Traits)
{
	// Outgoing runs back-to-front so that the innermost component (Handshake) is last to
	// transform, and CRC (the outermost wire wrapper) runs last.
	for (int32 i = Components.Num() - 1; i >= 0; --i)
	{
		if (Components[i].IsValid())
			Components[i]->Outgoing(Packet, Traits);
	}
}

void FSWGPacketHandler::Tick(float DeltaTime)
{
	for (TSharedPtr<HandlerComponent>& Comp : Components)
	{
		if (Comp.IsValid())
			Comp->Tick(DeltaTime);
	}
}

void FSWGPacketHandler::TriggerHandshake()
{
	for (TSharedPtr<HandlerComponent>& Comp : Components)
	{
		if (Comp.IsValid())
			Comp->NotifyHandshakeBegin();
	}
}

int32 FSWGPacketHandler::GetTotalReservedPacketBits() const
{
	int32 Total = 0;
	for (const TSharedPtr<HandlerComponent>& Comp : Components)
	{
		if (Comp.IsValid())
			Total += Comp->GetReservedPacketBits();
	}
	return Total;
}

void FSWGPacketHandler::OnSessionInitialized(const SessionData& Data)
{
	for (TSharedPtr<HandlerComponent>& Comp : Components)
	{
		const FString CompName = Comp->GetName().ToString();
		if (CompName == FSWGEncryptionComponent::GetComponentName())
		{
			InitalizeEncryptionHandler(Comp, Data);
		}
		else if (CompName == FSWGCrcComponent::GetComponentName())
		{
			InitalizeCrcHandler(Comp, Data);
		}
		else if (CompName == FSWGReliabilityComponent::GetComponentName())
		{
			InitalizeReliabilityHandler(Comp, Data);
		}
		else if (CompName == FSWGCompressionComponent::GetComponentName())
		{
			InitalizeCompressionHandler(Comp, Data);
		}
	}
}

void FSWGPacketHandler::InitalizeEncryptionHandler(TSharedPtr<HandlerComponent> Component, const SessionData& Data)
{
	TSharedPtr<FSWGEncryptionComponent> EncryptionComp = StaticCastSharedPtr<FSWGEncryptionComponent>(Component);
	if (!EncryptionComp.IsValid())
		return;

	TSharedPtr<FSWGSession> Session = SessionPtr.Pin();
	if (!Session.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FSWGPacketHandler: Session pointer is invalid"));
		return;
	}

	// Set encryption key and enable encryption
	Session->EncryptionKey = Data.EncryptionKey;

	FEncryptionData EncData;
	EncData.Key.SetNumUninitialized(sizeof(Data.EncryptionKey));
	FMemory::Memcpy(EncData.Key.GetData(), &Data.EncryptionKey, sizeof(Data.EncryptionKey));
	EncryptionComp->SetEncryptionData(EncData);
	EncryptionComp->EnableEncryption();
	EncryptionComp->SetActive(true);
}

void FSWGPacketHandler::InitalizeCompressionHandler(TSharedPtr<HandlerComponent> Component, const SessionData & Data)
{
	TSharedPtr<FSWGCompressionComponent> CompressionComp = StaticCastSharedPtr<FSWGCompressionComponent>(Component);
	if (!CompressionComp.IsValid())
		return;
	
	CompressionComp->SetActive(Data.UseCompression != 0);
}

void FSWGPacketHandler::InitalizeCrcHandler(TSharedPtr<HandlerComponent> Component, const SessionData & Data)
{
	// CRC is always active; nothing special to initialize

	TSharedPtr<FSWGCrcComponent> CrcComp = StaticCastSharedPtr<FSWGCrcComponent>(Component);
	if (!CrcComp.IsValid())
		return;
	CrcComp->SetEncryptionKey(Data.EncryptionKey);
	CrcComp->SetActive(true);
}

void FSWGPacketHandler::InitalizeReliabilityHandler(TSharedPtr<HandlerComponent> Component, const SessionData & Data)
{
	TSharedPtr<FSWGReliabilityComponent> ReliabilityComp = StaticCastSharedPtr<FSWGReliabilityComponent>(Component);
	if (!ReliabilityComp.IsValid())
		return;

	ReliabilityComp->SetActive(true);
}
