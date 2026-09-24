#include "Subsystems/SWGTravelSubsystem.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/TravelMessages.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGIffReader.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWGTravel, Log, All);

namespace
{
	FString CommandToken(FString Value)
	{
		Value.ReplaceInline(TEXT(" "), TEXT("_"));
		return Value;
	}

}

void USWGTravelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Network = Collection.InitializeDependency<USWGNetworkSubsystem>();
	Commands = Collection.InitializeDependency<USWGCommandSubsystem>();
	Tre = Collection.InitializeDependency<USWGTreSubsystem>();
	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGTravelSubsystem::HandleMessageReceived);
	}
}

void USWGTravelSubsystem::Deinitialize()
{
	if (Network)
	{
		Network->OnMessageReceived.Remove(MessageHandle);
	}
	Super::Deinitialize();
}

void USWGTravelSubsystem::LoadTravelTable()
{
	if (!Tre)
	{
		return;
	}

	FSWGDataTableData Table;
	const FSWGIffReader Reader = Tre->CreateIffReader(TEXT("datatables/travel/travel.iff"));
	if (!Reader.IsValid() || !FSWGDataTableReader::ReadDataTable(Reader, Table))
	{
		UE_LOG(LogSWGTravel, Warning, TEXT("failed to load datatables/travel/travel.iff"));
		return;
	}

	for (const FString& Column : Table.ColumnNames)
	{
		if (!Column.Equals(TEXT("Planet"), ESearchCase::IgnoreCase))
		{
			PlanetOrder.Add(Column.ToLower());
		}
	}
	for (int32 RowIndex = 0; RowIndex < Table.Rows.Num(); ++RowIndex)
	{
		const FString From = Table.GetCell(RowIndex, TEXT("Planet")).ToLower();
		for (const FString& To : PlanetOrder)
		{
			Fares.FindOrAdd(From).Add(To, FCString::Atoi(*Table.GetCell(RowIndex, To)));
		}
	}
}

void USWGTravelSubsystem::RequestPlanetLocations()
{
	if (!Network)
	{
		return;
	}
	for (const FString& Planet : PlanetOrder)
	{
		Network->SendMessage(FPlanetTravelPointListRequestMessage{ Planet }.Serialize());
	}
}

void USWGTravelSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message)
{
	if (!Message)
	{
		return;
	}

	switch (static_cast<ESWGMessageOp>(Message->Opcode))
	{
		case ESWGMessageOp::EnterTicketPurchaseMode:
		{
			const FEnterTicketPurchaseModeMessage& Enter = *static_cast<const FEnterTicketPurchaseModeMessage*>(Message.Get());
			DeparturePlanet = Enter.DeparturePlanet.ToLower();
			DepartureLocation = Enter.DepartureLocation;
			DestinationsByPlanet.Reset();
			bDepartureInterplanetary = false;
			// Not at Initialize: the TRE archives mount after the subsystems do.
			if (PlanetOrder.IsEmpty())
			{
				LoadTravelTable();
			}
			RequestPlanetLocations();
			OnTravelWindowRequested.Broadcast();
			OnTravelDataChanged.Broadcast();
			UE_LOG(LogSWGTravel, Log, TEXT("ticket purchase opened at %s / %s"), *DeparturePlanet, *DepartureLocation);
			break;
		}
		case ESWGMessageOp::PlanetTravelPointListResponse:
		{
			const FPlanetTravelPointListResponseMessage& Response = *static_cast<const FPlanetTravelPointListResponseMessage*>(Message.Get());
			const FString Planet = Response.Planet.ToLower();
			TArray<FSWGTravelDestination>& Destinations = DestinationsByPlanet.FindOrAdd(Planet);
			Destinations.Reset();
			for (const FSWGPlanetTravelPoint& Point : Response.Points)
			{
				FSWGTravelDestination& Destination = Destinations.AddDefaulted_GetRef();
				Destination.Planet = Planet;
				Destination.Location = Point.Name;
				Destination.Position = FVector2D(Point.X, Point.Y);
				Destination.bInterplanetary = Point.bInterplanetary;
				if (Planet == DeparturePlanet && Point.Name.Equals(DepartureLocation, ESearchCase::IgnoreCase))
				{
					bDepartureInterplanetary = Destination.bInterplanetary;
				}
			}
			Destinations.Sort([](const FSWGTravelDestination& A, const FSWGTravelDestination& B)
			{
				return A.Location < B.Location;
			});
			OnTravelDataChanged.Broadcast();
			break;
		}
		default:
			break;
	}
}

TArray<FString> USWGTravelSubsystem::GetAvailablePlanets() const
{
	TArray<FString> Result;
	for (const FString& Planet : PlanetOrder)
	{
		// travel.iff is sparse: a 0 fare is a route Core3 rejects outright
		// (Tatooine flies only to Corellia, Lok and Naboo).
		if (Planet == DeparturePlanet || (bDepartureInterplanetary && GetFare(Planet, false) > 0))
		{
			Result.Add(Planet);
		}
	}
	return Result;
}

TArray<FSWGTravelDestination> USWGTravelSubsystem::GetDestinations(const FString& Planet) const
{
	TArray<FSWGTravelDestination> Result = DestinationsByPlanet.FindRef(Planet.ToLower());
	Result.RemoveAll([this](const FSWGTravelDestination& Destination)
	{
		// Mirrors Core3's isTravelToLocationPermitted: off-planet arrivals must be interplanetary points.
		return (Destination.Planet == DeparturePlanet && Destination.Location.Equals(DepartureLocation, ESearchCase::IgnoreCase))
			|| (Destination.Planet != DeparturePlanet && !Destination.bInterplanetary);
	});
	return Result;
}

int32 USWGTravelSubsystem::GetFare(const FString& ArrivalPlanet, bool bRoundTrip) const
{
	const TMap<FString, int32>* From = Fares.Find(DeparturePlanet);
	const int32* Fare = From ? From->Find(ArrivalPlanet.ToLower()) : nullptr;
	return Fare ? *Fare * (bRoundTrip ? 2 : 1) : 0;
}

bool USWGTravelSubsystem::CanBuyRoundTrip(const FSWGTravelDestination& Destination) const
{
	return Destination.Planet == DeparturePlanet || Destination.bInterplanetary;
}

FString USWGTravelSubsystem::BuildPurchaseArguments(const FString& FromPlanet, const FString& FromLocation,
	const FString& ToPlanet, const FString& ToLocation, bool bRoundTrip)
{
	return FString::Printf(TEXT("%s %s %s %s %s"), *CommandToken(FromPlanet), *CommandToken(FromLocation),
		*CommandToken(ToPlanet), *CommandToken(ToLocation), bRoundTrip ? TEXT("round") : TEXT("single"));
}

bool USWGTravelSubsystem::PurchaseTicket(const FSWGTravelDestination& Destination, bool bRoundTrip)
{
	if (!Commands || DeparturePlanet.IsEmpty() || DepartureLocation.IsEmpty() || Destination.Location.IsEmpty()
		|| GetFare(Destination.Planet, bRoundTrip) <= 0 || (bRoundTrip && !CanBuyRoundTrip(Destination)))
	{
		return false;
	}
	const FString Arguments = BuildPurchaseArguments(DeparturePlanet, DepartureLocation,
		Destination.Planet, Destination.Location, bRoundTrip);
	return Commands->SendCommand(TEXT("purchaseTicket"), 0, Arguments) != 0;
}
