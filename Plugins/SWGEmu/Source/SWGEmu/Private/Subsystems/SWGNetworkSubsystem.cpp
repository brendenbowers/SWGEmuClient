#include "Subsystems/SWGNetworkSubsystem.h"
#include "Network/Handler/SWGPacketHandler.h"
#include "Network/SWGSocketReader.h"
#include "Network/SWGSessionOp.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/RunnableThread.h"
#include "Serialization/BitWriter.h"
#include "PacketHandler.h"

USWGNetworkSubsystem::USWGNetworkSubsystem() = default;
USWGNetworkSubsystem::~USWGNetworkSubsystem() = default;

void USWGNetworkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("SWGNetworkSubsystem: initialized"));
}

void USWGNetworkSubsystem::Deinitialize()
{
	Disconnect();
	Super::Deinitialize();
	UE_LOG(LogTemp, Log, TEXT("SWGNetworkSubsystem: deinitialized"));
}

// ── Connection ────────────────────────────────────────────────────────────────

bool USWGNetworkSubsystem::Connect(const FString& HostAddress, int32 Port, FString& OutError)
{
	if (IsConnected())
	{
		OutError = TEXT("Already connected — disconnect first");
		return false;
	}

	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	// 1. Create a UDP socket.
	Socket = SS->CreateSocket(NAME_DGram, TEXT("SWGEmu"), /*bForceUDP=*/false);
	if (!Socket)
	{
		OutError = TEXT("Failed to create UDP socket");
		return false;
	}

	// 2. Resolve server address.
	TSharedPtr<FInternetAddr> RemoteAddr;
	{
		TSharedRef<FInternetAddr> Candidate = SS->CreateInternetAddr();
		bool bAddrValid = false;
		Candidate->SetIp(*HostAddress, bAddrValid);
		if (bAddrValid)
		{
			RemoteAddr = Candidate;
		}
		else
		{
			FAddressInfoResult AddrResult = SS->GetAddressInfo(*HostAddress, nullptr,
				EAddressInfoFlags::Default, NAME_None);
			if (AddrResult.Results.Num() == 0)
			{
				OutError = FString::Printf(TEXT("Could not resolve host: %s"), *HostAddress);
				SS->DestroySocket(Socket);
				Socket = nullptr;
				return false;
			}
			RemoteAddr = AddrResult.Results[0].Address;
		}
	}
	RemoteAddr->SetPort(Port);

	// 3. "Connect" the UDP socket to set the default remote endpoint.
	if (!Socket->Connect(*RemoteAddr))
	{
		OutError = TEXT("Socket connect failed");
		SS->DestroySocket(Socket);
		Socket = nullptr;
		return false;
	}

	// 4. Reset session state and create the handler pipeline.
	Session.Reset();
	PacketHandler = MakeUnique<FSWGPacketHandler>(&Session);
	PacketHandler->Initialize();

	// 5. Trigger the handshake: HandshakeComponent::NotifyHandshakeBegin() queues
	//    a SessionRequest to Session.OutgoingUnreliable.
	PacketHandler->TriggerHandshake();

	// 6. Flush the outgoing queue once so the SessionRequest actually gets sent before
	//    we start the reader thread. (The game-thread Tick won't run until we return.)
	{
		FSWGPacket Pkt;
		while (Session.OutgoingUnreliable.Dequeue(Pkt))
		{
			SendPacketThroughPipeline(Pkt);
		}
	}

	// 7. Start the reader thread (now safe to receive SessionResponse).
	ReaderRunnable = MakeUnique<FSWGSocketReader>(Socket, PacketHandler.Get());
	ReaderThread = FRunnableThread::Create(
		ReaderRunnable.Get(), TEXT("SWGSocketReader"),
		/*StackSize=*/0, TPri_AboveNormal);

	if (!ReaderThread)
	{
		OutError = TEXT("Failed to start reader thread");
		StopIOThreads();
		return false;
	}

	// 8. Block until the handshake completes or times out (5 seconds).
	const double TimeoutSec = 5.0;
	const double StartSec   = FPlatformTime::Seconds();
	while (Session.State != ESWGSessionState::Connected)
	{
		if (Session.State == ESWGSessionState::Error)
		{
			OutError = TEXT("Handshake failed — session error");
			StopIOThreads();
			return false;
		}
		if (FPlatformTime::Seconds() - StartSec > TimeoutSec)
		{
			OutError = TEXT("Handshake timed out");
			StopIOThreads();
			return false;
		}
		// Flush any ACKs or other control packets queued by the pipeline components
		// while we wait (e.g., the reliability component may enqueue DataAcks).
		FSWGPacket Pkt;
		while (Session.OutgoingUnreliable.Dequeue(Pkt))
			SendPacketThroughPipeline(Pkt);

		FPlatformProcess::SleepNoStats(0.005f);
	}

	UE_LOG(LogTemp, Log, TEXT("SWGNetworkSubsystem: connected to %s:%d (key=0x%08X)"),
		*HostAddress, Port, Session.EncryptionKey);
	return true;
}

void USWGNetworkSubsystem::Disconnect()
{
	if (!IsConnected())
		return;

	// Send a SOE Disconnect packet.
	FSWGPacket DiscoPacket;
	DiscoPacket.WriteByte(0x00);
	DiscoPacket.WriteByte(static_cast<uint8>(ESWGSessionOp::Disconnect));
	DiscoPacket.WriteByte(0x00);
	DiscoPacket.WriteByte(0x00);
	DiscoPacket.WriteByte(0x00);
	DiscoPacket.WriteByte(0x00);
	SendPacketThroughPipeline(DiscoPacket);

	StopIOThreads();
	Session.Reset();

	if (Socket)
	{
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}

	UE_LOG(LogTemp, Log, TEXT("SWGNetworkSubsystem: disconnected"));
}

bool USWGNetworkSubsystem::IsConnected() const
{
	return Session.IsConnected();
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void USWGNetworkSubsystem::Tick(float DeltaTime)
{
	// Let reliability component trigger retransmits.
	if (PacketHandler)
		PacketHandler->Tick(DeltaTime);

	// Drain outgoing queues → pipeline → socket.
	FlushOutgoingQueues();

	// Dispatch fully reassembled incoming game messages.
	ProcessIncomingMessages();
}

TStatId USWGNetworkSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGNetworkSubsystem, STATGROUP_Tickables);
}

bool USWGNetworkSubsystem::IsTickable() const
{
	return Session.State != ESWGSessionState::Disconnected;
}

// ── Message Registration & Send ───────────────────────────────────────────────

void USWGNetworkSubsystem::RegisterMessageHandler(uint32 Opcode, FMessageHandler&& Handler)
{
	MessageHandlers.Add(Opcode, MoveTemp(Handler));
}

void USWGNetworkSubsystem::SendMessage(const FSWGPacket& Message, bool bReliable)
{
	if (!IsConnected())
		return;

	if (bReliable)
		Session.OutgoingReliable.Enqueue(Message);
	else
		Session.OutgoingUnreliable.Enqueue(Message);
}

// ── Internal ──────────────────────────────────────────────────────────────────

void USWGNetworkSubsystem::FlushOutgoingQueues()
{
	if (!Socket || !PacketHandler)
		return;

	FSWGPacket Pkt;

	// Unreliable first (control packets, ACKs).
	while (Session.OutgoingUnreliable.Dequeue(Pkt))
		SendPacketThroughPipeline(Pkt);

	// Reliable second (game messages via DataChannel1).
	while (Session.OutgoingReliable.Dequeue(Pkt))
		SendPacketThroughPipeline(Pkt);
}

void USWGNetworkSubsystem::SendPacketThroughPipeline(const FSWGPacket& Pkt)
{
	// Copy the packet bytes into an FBitWriter so the pipeline components can
	// transform them in-place (Reliability → Compression → Encryption → CRC).
	const int32 NumBytes = Pkt.Data.Num();
	if (NumBytes == 0)
		return;

	FBitWriter Writer(NumBytes * 8 + 64, /*AllowResize=*/true);

	// Serialize the raw bytes into the writer.
	Writer.Serialize(const_cast<uint8*>(Pkt.Data.GetData()), NumBytes);

	FOutPacketTraits Traits;
	Traits.bAllowCompression = false; // Phase 1: compression disabled

	PacketHandler->Outgoing(Writer, Traits);

	// Transmit the resulting bytes.
	const int32 TotalBytes = (int32)Writer.GetNumBytes();
	if (TotalBytes > 0)
	{
		int32 BytesSent = 0;
		Socket->Send(Writer.GetData(), TotalBytes, BytesSent);
		Session.LastPacketSent = (uint64)(FPlatformTime::Seconds() * 1000.0);
	}
}

void USWGNetworkSubsystem::ProcessIncomingMessages()
{
	FSWGPacket Pkt;
	while (Session.IncomingMessages.Dequeue(Pkt))
	{
		FSWGMessage Msg(Pkt);
		OnMessageReceived.Broadcast(Msg);

		if (FMessageHandler* HandlerFn = MessageHandlers.Find(Msg.Opcode))
		{
			(*HandlerFn)(Msg);
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("SWGNetworkSubsystem: unhandled opcode 0x%08X"), Msg.Opcode);
		}
	}
}

void USWGNetworkSubsystem::StopIOThreads()
{
	if (ReaderRunnable)
		ReaderRunnable->Stop();

	if (ReaderThread)
	{
		ReaderThread->WaitForCompletion();
		delete ReaderThread;
		ReaderThread = nullptr;
	}

	ReaderRunnable.Reset();
	PacketHandler.Reset();
}
