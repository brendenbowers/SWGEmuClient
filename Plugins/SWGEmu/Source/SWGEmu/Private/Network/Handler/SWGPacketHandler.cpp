#include "Network/Handler/SWGPacketHandler.h"
#include "Network/Handler/SWGCrcComponent.h"
#include "Network/Handler/SWGEncryptionComponent.h"
#include "Network/Handler/SWGCompressionComponent.h"
#include "Network/Handler/SWGReliabilityComponent.h"
#include "Network/Handler/SWGHandshakeComponent.h"
#include "Network/SWGSession.h"

FSWGPacketHandler::FSWGPacketHandler(FSWGSession* InSession)
	: Session(InSession)
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

	auto Crc         = MakeShared<FSWGCrcComponent>(Session);
	auto Encryption  = MakeShared<FSWGEncryptionComponent>(Session);
	auto Compression = MakeShared<FSWGCompressionComponent>(Session);
	auto Reliability = MakeShared<FSWGReliabilityComponent>(Session);
	auto Handshake   = MakeShared<FSWGHandshakeComponent>(Session, &Encryption.Get());

	Components.Empty();
	Components.Add(Crc);
	Components.Add(Encryption);
	Components.Add(Compression);
	Components.Add(Reliability);
	Components.Add(Handshake);

	for (TSharedPtr<HandlerComponent>& Comp : Components)
	{
		Comp->Initialize();
	}

	const int32 ReservedBits = GetTotalReservedPacketBits();
	const int32 MaxPayloadBytes = (int32)Session->MaxPacketSize - (ReservedBits / 8);
	UE_LOG(LogTemp, Log,
		TEXT("FSWGPacketHandler: initialized — reserved %d bits, max payload %d bytes"),
		ReservedBits, MaxPayloadBytes);
}

void FSWGPacketHandler::Incoming(FBitReader& Packet)
{
	for (TSharedPtr<HandlerComponent>& Comp : Components)
	{
		if (Packet.IsError())
			break;

		if (Comp.IsValid())
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
