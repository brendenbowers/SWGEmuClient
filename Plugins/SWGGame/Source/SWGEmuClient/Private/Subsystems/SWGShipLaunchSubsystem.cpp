#include "Subsystems/SWGShipLaunchSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGIntangibleObjectSubsystem.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Components/SWGTangibleComponent.h"
#include "Objects/World/SWGShip.h"
#include "Objects/Player/SWGPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/ObjControllerMessageIn.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"

void USWGShipLaunchSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Network = Collection.InitializeDependency<USWGNetworkSubsystem>();
	Collection.InitializeDependency<USWGObjectGraphSubsystem>();
	Collection.InitializeDependency<USWGIntangibleObjectSubsystem>();
	MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGShipLaunchSubsystem::HandleMessage);
}
void USWGShipLaunchSubsystem::Deinitialize()
{
	if (Network) Network->OnMessageReceived.Remove(MessageHandle);
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RequestTimer);
}
int64 USWGShipLaunchSubsystem::ResolveControlDevice(const USWGObjectGraphSubsystem& Graph, int64 ShipId)
{
	const int64* Parent = Graph.FindContainerId(ShipId);
	const int32* Type = Graph.FindContainmentType(ShipId);
	if (!Parent || !*Parent || !Type || *Type != int32(ESWGContainmentType::Rider)
		|| !Cast<ASWGShip>(Graph.FindActor(ShipId)) || !Graph.IsOwnedByLocalPlayer(ShipId)) return 0;
	return *Parent;
}
bool USWGShipLaunchSubsystem::RequestShips(int64 TerminalId)
{
	USWGObjectGraphSubsystem* Graph = GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>();
	if (!Network || !GetWorld() || !Graph || !Graph->GetLocalPlayerObjectId()
		|| USWGTangibleComponent::GetGameObjectType(Graph->FindActor(TerminalId)) != 0x400B) return false;
	RequestedTerminal = TerminalId; List = {}; bWaiting = true; Status = TEXT("Requesting ships...");
	if (ASWGPlayer* Player = Cast<ASWGPlayer>(Graph->FindActor(Graph->GetLocalPlayerObjectId()))) Player->SendDataTransformUpdate();
	Network->SendMessage(FJtlShipListRequest(Graph->GetLocalPlayerObjectId(), TerminalId).Serialize());
	GetWorld()->GetTimerManager().SetTimer(RequestTimer, this, &USWGShipLaunchSubsystem::HandleTimeout, 10.f, false);
	OnWindowRequested.Broadcast(); OnDataChanged.Broadcast();
	return true;
}
void USWGShipLaunchSubsystem::HandleTimeout()
{
	if (!bWaiting) return;
	bWaiting = false; Status = TEXT("The terminal did not respond. Use it again to retry.");
	OnDataChanged.Broadcast();
}
void USWGShipLaunchSubsystem::HandleMessage(TSharedPtr<FSWGNetMessage> Message)
{
	if (!Message) return;
	if (Message->Opcode == uint32(ESWGMessageOp::CmdStartScene))
	{
		if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RequestTimer);
		RequestedTerminal = 0; List = {}; bWaiting = false; Status.Empty();
		OnDataChanged.Broadcast(); return;
	}
	if (!RequestedTerminal) return;
	if (Message->Opcode == uint32(ESWGMessageOp::ObjControllerMessage))
	{
		const auto& Envelope = *static_cast<const FObjControllerMessageIn*>(Message.Get());
		USWGObjectGraphSubsystem* Graph = GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>();
		if (Envelope.Type != FJtlShipListResponse::ControllerType || !Graph || Envelope.ObjectId != Graph->GetLocalPlayerObjectId()) return;
		FSWGPacket Payload = Envelope.AsPayloadPacket(); FJtlShipListResponse Parsed;
		if (!Parsed.Deserialize(Payload) || Parsed.TerminalId != uint64(RequestedTerminal)) return;
		List = MoveTemp(Parsed); bWaiting = false;
		if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RequestTimer);
		Status = List.Ships.IsEmpty() ? TEXT("No ships are available.") : TEXT("Select a ship parked at this starport.");
		OnDataChanged.Broadcast();
	}
	else if (Message->Opcode == uint32(ESWGMessageOp::UpdateContainmentMessage)
		|| Message->Opcode == uint32(ESWGMessageOp::BaselinesMessage) || Message->Opcode == uint32(ESWGMessageOp::SceneEndBaselines)
		|| Message->Opcode == uint32(ESWGMessageOp::SceneDestroyObject))
		OnDataChanged.Broadcast();
}
TArray<FSWGLaunchShip> USWGShipLaunchSubsystem::GetShips() const
{
	TArray<FSWGLaunchShip> Result;
	USWGObjectGraphSubsystem* Graph = GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>();
	USWGIntangibleObjectSubsystem* Intangibles = GetGameInstance()->GetSubsystem<USWGIntangibleObjectSubsystem>();
	if (!Graph) return Result;
	for (const FJtlShipListEntry& Row : List.Ships)
	{
		FSWGLaunchShip Entry;
		Entry.ShipId = int64(Row.ShipId); Entry.ParkingLocation = Row.ParkingLocation;
		Entry.ControlDeviceId = ResolveControlDevice(*Graph, Entry.ShipId);
		const FSWGIntangibleEntry* Device = Intangibles ? Intangibles->FindEntry(Entry.ControlDeviceId) : nullptr;
		Entry.Name = Device && !Device->Name.IsEmpty() ? Device->Name : TEXT("Ship");
		Entry.bCanLaunch = Entry.ControlDeviceId != 0 && !List.PlayerLocation.IsEmpty()
			&& Entry.ParkingLocation.Equals(List.PlayerLocation, ESearchCase::IgnoreCase);
		Result.Add(MoveTemp(Entry));
	}
	return Result;
}
bool USWGShipLaunchSubsystem::Launch(int64 ShipId)
{
	if (!RequestedTerminal || bWaiting) return false;
	const TArray<FSWGLaunchShip> Ships = GetShips();
	const FSWGLaunchShip* Ship = Ships.FindByPredicate([&](const auto& Entry) { return Entry.ShipId == ShipId; });
	USWGCommandSubsystem* Commands = GetGameInstance()->GetSubsystem<USWGCommandSubsystem>();
	if (!Ship || !Ship->bCanLaunch || !Commands) return false;
	return Commands->SendCommand(TEXT("launchIntoSpace"), RequestedTerminal,
		FString::Printf(TEXT("%lld 0"), Ship->ControlDeviceId)) != 0;
}
