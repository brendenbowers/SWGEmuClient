#include "Subsystems/SWGRegionSubsystem.h"

#include "Common/SWGWorldScale.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	constexpr float TickSeconds = 0.25f;
}

void USWGRegionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Tre = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));
	Terrain = Cast<USWGTerrainSubsystem>(Collection.InitializeDependency(USWGTerrainSubsystem::StaticClass()));
	ObjectGraph = Cast<USWGObjectGraphSubsystem>(Collection.InitializeDependency(USWGObjectGraphSubsystem::StaticClass()));
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USWGRegionSubsystem::Tick), TickSeconds);
}

void USWGRegionSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Super::Deinitialize();
}

FString USWGRegionSubsystem::ResolveName(const FSWGClientRegion& Region) const
{
	return Tre ? Tre->ResolveStringId(Region.NameReference) : Region.NameReference;
}

void USWGRegionSubsystem::EnsureLoaded()
{
	const FString Planet = Terrain ? Terrain->GetActivePlanetName().ToLower() : FString();
	if (Planet.IsEmpty() || Planet == LoadedPlanet || !Tre)
	{
		return;
	}
	LoadedPlanet = Planet;
	CurrentIndex = INDEX_NONE;
	CurrentName.Reset();
	Regions.Regions.Reset();

	// Zones without a table (e.g. the creature_test sandbox) simply have no regions.
	const FString Path = FString::Printf(TEXT("datatables/clientregion/%s.iff"), *Planet);
	FSWGDataTableData Table;
	if (Tre->FileExists(Path) && FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(Path), Table))
	{
		Regions.LoadFromTable(Table);
	}
}

FString USWGRegionSubsystem::FindRegionName(const FVector2D& RawPosition, float* OutRadius) const
{
	const int32 Index = Regions.FindContaining(RawPosition);
	if (Index == INDEX_NONE)
	{
		return FString();
	}
	if (OutRadius)
	{
		*OutRadius = Regions.Regions[Index].Radius;
	}
	return ResolveName(Regions.Regions[Index]);
}

bool USWGRegionSubsystem::Tick(float DeltaSeconds)
{
	EnsureLoaded();
	const AActor* Player = ObjectGraph ? ObjectGraph->FindActor(ObjectGraph->GetLocalPlayerObjectId()) : nullptr;
	if (!Player || Regions.Regions.IsEmpty())
	{
		return true;
	}

	const FVector Raw = SWGToRawSpace(Player->GetActorLocation());
	const int32 Index = Regions.FindContaining(FVector2D(Raw.X, Raw.Y));
	if (Index != CurrentIndex)
	{
		const FString Left = CurrentName;
		CurrentIndex = Index;
		CurrentName = Index == INDEX_NONE ? FString() : ResolveName(Regions.Regions[Index]);
		UE_LOG(LogTemp, Log, TEXT("USWGRegionSubsystem: '%s' -> '%s'"), *Left, *CurrentName);
		OnRegionChanged.Broadcast(Left, CurrentName);
	}
	return true;
}
