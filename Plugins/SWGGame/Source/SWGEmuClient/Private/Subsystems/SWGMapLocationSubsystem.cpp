#include "Subsystems/SWGMapLocationSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Subsystems/SubsystemCollection.h"

void USWGMapLocationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Network = Collection.InitializeDependency<USWGNetworkSubsystem>();
	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGMapLocationSubsystem::HandleMessageReceived);
	}
}

void USWGMapLocationSubsystem::Deinitialize()
{
	if (Network)
	{
		Network->OnMessageReceived.Remove(MessageHandle);
	}
	Super::Deinitialize();
}

void USWGMapLocationSubsystem::RequestPlanet(const FString& Planet)
{
	if (Network && Network->IsConnected() && !Planet.IsEmpty())
	{
		Network->SendMessage(FGetMapLocationsRequestMessage{ Planet.ToLower() }.Serialize());
	}
}

void USWGMapLocationSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message)
{
	if (!Message || Message->Opcode != static_cast<uint32>(ESWGMessageOp::GetMapLocationsResponse))
	{
		return;
	}
	const FGetMapLocationsResponseMessage& Response = *static_cast<const FGetMapLocationsResponseMessage*>(Message.Get());
	if (Response.bValid)
	{
		LocationsByPlanet.Add(Response.Planet, Response.Locations);
		OnLocationsChanged.Broadcast(Response.Planet);
	}
}
