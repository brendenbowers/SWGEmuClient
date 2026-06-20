#include "Subsystems/SWGNetworkSubsystem.h"
#include "Network/SWGSocketReader.h"
#include "Network/SWGSocketWriter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

USWGNetworkSubsystem::USWGNetworkSubsystem() = default;

// Out-of-line dtor: here FSWGSocketReader / FSWGSocketWriter are complete types,
// so the TUniquePtr members can be destroyed correctly.
USWGNetworkSubsystem::~USWGNetworkSubsystem() = default;

void USWGNetworkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogTemp, Log, TEXT("SWGNetworkSubsystem initialized"));

	// TODO: Phase 2 - Register message handlers
	// RegisterMessageHandler((uint32)ESWGMessageOp::CmdStartScene, ...);
	// RegisterMessageHandler((uint32)ESWGMessageOp::BaselinesMessage, ...);
	// etc.
}

void USWGNetworkSubsystem::Deinitialize()
{
	Disconnect();
	Super::Deinitialize();

	UE_LOG(LogTemp, Log, TEXT("SWGNetworkSubsystem deinitialize"));
}

bool USWGNetworkSubsystem::Connect(const FString& HostAddress, int32 Port, FString& OutError)
{
	// TODO: Phase 1 - Establish UDP connection
	// - Create socket
	// - Resolve host address
	// - Perform SOE handshake
	// - Start reader/writer threads

	OutError = TEXT("Not yet implemented");
	return false;
}

void USWGNetworkSubsystem::Disconnect()
{
	if (!IsConnected())
		return;

	// TODO: Phase 1 - Send Disconnect packet and cleanup
	StopIOThreads();
	Session.Reset();

	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

bool USWGNetworkSubsystem::IsConnected() const
{
	return Session.IsConnected();
}

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

void USWGNetworkSubsystem::Tick(float DeltaTime)
{
	// FTickableGameObject::Tick is pure virtual; there is no base Tick to call.
	ProcessIncomingMessages();
}

TStatId USWGNetworkSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGNetworkSubsystem, STATGROUP_Tickables);
}

bool USWGNetworkSubsystem::IsTickable() const
{
	// Only tick once a session exists; avoids ticking the CDO and idle instances.
	return Session.State != ESWGSessionState::Disconnected;
}

void USWGNetworkSubsystem::ProcessIncomingMessages()
{
	// TODO: Phase 2 - Dequeue messages and dispatch to handlers
	FSWGPacket Pkt;
	while (Session.IncomingMessages.Dequeue(Pkt))
	{
		FSWGMessage Msg(Pkt);
		OnMessageReceived.Broadcast(Msg);

		auto* Handler = MessageHandlers.Find(Msg.Opcode);
		if (Handler)
		{
			(*Handler)(Msg);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Unhandled message opcode: 0x%08X"), Msg.Opcode);
		}
	}
}

bool USWGNetworkSubsystem::PerformHandshake()
{
	// TODO: Phase 1 - SOE SessionRequest/SessionResponse handshake
	return false;
}

void USWGNetworkSubsystem::StartIOThreads()
{
	// TODO: Phase 1 - Create and start reader/writer FRunnableThreads
}

void USWGNetworkSubsystem::StopIOThreads()
{
	if (ReaderThread)
	{
		ReaderThread->WaitForCompletion();
		delete ReaderThread;
		ReaderThread = nullptr;
		ReaderRunnable = nullptr;
	}

	if (WriterThread)
	{
		WriterThread->WaitForCompletion();
		delete WriterThread;
		WriterThread = nullptr;
		WriterRunnable = nullptr;
	}
}
