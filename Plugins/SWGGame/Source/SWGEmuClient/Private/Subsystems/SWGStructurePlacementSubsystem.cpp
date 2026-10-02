#include "Subsystems/SWGStructurePlacementSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/StructureMessages.h"
#include "Network/Messages/Zone/AttributeListMessageIn.h"
#include "Common/SWGWorldScale.h"
#include "TRE/SWGDataTableReader.h"
#include "Structure/SWGPlacementRules.h"
#include "Objects/SWGObject.h"
#include "Objects/World/SWGStructurePlacementPreview.h"
#include "Objects/Creature/SWGCreature.h"
#include "Components/SWGCombatStateComponent.h"
#include "Components/SWGTangibleComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"

void USWGStructurePlacementSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Network = Collection.InitializeDependency<USWGNetworkSubsystem>();
	Commands = Collection.InitializeDependency<USWGCommandSubsystem>();
	Tre = Collection.InitializeDependency<USWGTreSubsystem>();
	Meshes = Collection.InitializeDependency<USWGMeshGeneratorSubsystem>();
	Terrain = Collection.InitializeDependency<USWGTerrainSubsystem>();
	ObjectGraph = Collection.InitializeDependency<USWGObjectGraphSubsystem>();
	MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGStructurePlacementSubsystem::HandleMessageReceived);
	ObjectDestroyedHandle = ObjectGraph->OnObjectDestroyed.AddUObject(this, &USWGStructurePlacementSubsystem::HandleObjectDestroyed);
	ObjectReadyHandle = ObjectGraph->OnObjectReady.AddUObject(this, &USWGStructurePlacementSubsystem::HandleObjectReady);
	Terrain->OnZoneReset.AddUObject(this, &USWGStructurePlacementSubsystem::Cancel);
	Terrain->OnZoneReset.AddUObject(this, &USWGStructurePlacementSubsystem::ClearPendingMarkers);
}

void USWGStructurePlacementSubsystem::Deinitialize()
{
	Network->OnMessageReceived.Remove(MessageHandle);
	ObjectGraph->OnObjectDestroyed.Remove(ObjectDestroyedHandle);
	ObjectGraph->OnObjectReady.Remove(ObjectReadyHandle);
	Terrain->OnZoneReset.RemoveAll(this);
	Cancel();
	ClearPendingMarkers();
	Super::Deinitialize();
}

TStatId USWGStructurePlacementSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWGStructurePlacementSubsystem, STATGROUP_Tickables);
}

void USWGStructurePlacementSubsystem::Tick(float DeltaTime)
{
	TickPendingMarkers(DeltaTime);
	if (!IsActive()) { return; }
	const int64 PlayerId = ObjectGraph->GetLocalPlayerObjectId();
	const ASWGCreature* Player = Cast<ASWGCreature>(ObjectGraph->FindActor(PlayerId));
	const AActor* PlacementPlayer = Player ? static_cast<const AActor*>(Player) : bFake && GetWorld() && GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
	if (bFake && !Player && !bFakeGrounded && PlacementPlayer && Terrain->GetPlanetData())
	{
		AActor* Pawn = const_cast<AActor*>(PlacementPlayer);
		const FVector Raw = SWGToRawSpace(Pawn->GetActorLocation());
		Pawn->SetActorLocation(SWGToUnrealSpace(FVector(Raw.X, Raw.Y, Terrain->GetHeightAt(Raw.X, Raw.Y) + 2.f)));
		bFakeGrounded = true;
		SetCandidate(FVector2D(Raw.X, Raw.Y));
	}
	const int64* ParentId = ObjectGraph->FindContainerId(PlayerId);
	if (!PlacementPlayer || (Player && ParentId && *ParentId != 0)
		|| (Player && Player->CombatStateComponent && (Player->CombatStateComponent->IsDead() || Player->CombatStateComponent->IsIncapacitated()))
		|| (!bFake && !ObjectGraph->IsOwnedByLocalPlayer(static_cast<int64>(DeedId))))
	{
		Cancel();
	}
}

void USWGStructurePlacementSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message)
{
	if (Message && Message->Opcode == static_cast<uint32>(ESWGMessageOp::AttributeListMessage) && IsActive() && !bFake)
	{
		const auto& List = *static_cast<const FAttributeListMessageIn*>(Message.Get());
		if (List.ObjectId != static_cast<int64>(DeedId)) { return; }
		bPlanetKnown = true;
		const FString Planet = Terrain->GetActivePlanetName();
		// Core3's StructureManager skips the planet check on the creature_test sandbox zone.
		bPlanetAllowed = Planet.Equals(TEXT("creature_test"), ESearchCase::IgnoreCase);
		for (const FSWGObjectAttribute& Attribute : List.Attributes)
		{
			if (Attribute.Name == TEXT("examine_scene") && Attribute.Value.Equals(FString(TEXT("@planet_n:")) + Planet, ESearchCase::IgnoreCase))
			{
				bPlanetAllowed = true;
				break;
			}
		}
		Validate();
		if (Preview) { Preview->UpdatePlacement(Candidate, Rotation, Footprint, ClearFloraRadius, bSnapToTerrain, Validation.Verdict); }
		OnPlacementUpdated.Broadcast();
		return;
	}
	if (Message && Message->Opcode == static_cast<uint32>(ESWGMessageOp::EnterStructurePlacementMode))
	{
		const auto& Enter = *static_cast<const FEnterStructurePlacementModeMessage*>(Message.Get());
		BeginFromServer(Enter.DeedId, Enter.ClientTemplatePath);
	}
}

bool USWGStructurePlacementSubsystem::BeginFromServer(uint64 InDeedId, const FString& InTemplatePath)
{
	if (!InDeedId || !InTemplatePath.StartsWith(TEXT("object/"))) { return false; }
	Cancel();
	DeedId = InDeedId;
	bFake = InDeedId == TNumericLimits<uint64>::Max();
	bFakeGrounded = false;
	bPlanetKnown = bFake;
	bPlanetAllowed = bFake;
	TemplatePath = InTemplatePath;
	Rotation = 0;
	Footprint = {};
	ClearFloraRadius = 0.f;
	bSnapToTerrain = false;
	Meshes->ResolveTemplateFloatParam(TemplatePath, SWG_IFF_TAG('S','H','O','T'), TEXT("clearFloraRadius"), ClearFloraRadius);
	Meshes->ResolveTemplateBoolParam(TemplatePath, SWG_IFF_TAG('S','H','O','T'), TEXT("snapToTerrain"), bSnapToTerrain);
	FString FootprintPath;
	if (Meshes->ResolveTemplateStringParam(TemplatePath, SWG_IFF_TAG('S','T','O','T'), TEXT("structureFootprintFileName"), FootprintPath)
		&& !FootprintPath.IsEmpty())
	{
		FSWGFootprintReader::Read(Tre->CreateIffReader(FootprintPath), Footprint);
	}
	const AActor* Player = ObjectGraph->FindActor(ObjectGraph->GetLocalPlayerObjectId());
	if (!Player && bFake && GetWorld() && GetWorld()->GetFirstPlayerController()) { Player = GetWorld()->GetFirstPlayerController()->GetPawn(); }
	const FVector PlayerRaw = Player ? SWGToRawSpace(Player->GetActorLocation()) : FVector::ZeroVector;
	Candidate = FVector2D(PlayerRaw.X, PlayerRaw.Y);
	LoadPlanetRules();
	if (!bFake) { Commands->SendCommand(TEXT("getattributesbatch"), 0, FString::Printf(TEXT("%llu"), DeedId)); }
	Validate();
	if (UWorld* World = GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		Preview = World->SpawnActor<ASWGStructurePlacementPreview>(Params);
		if (Preview)
		{
			Preview->Initialize(TemplatePath, Meshes, Terrain, ObjectGraph, this);
			Preview->UpdatePlacement(Candidate, Rotation, Footprint, ClearFloraRadius, bSnapToTerrain, Validation.Verdict);
		}
	}
	OnPlacementStarted.Broadcast();
	return true;
}

bool USWGStructurePlacementSubsystem::BeginFake(const FString& InTemplatePath)
{
	// A preview can be inspected in PIE even when the local Core3 server is down.
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController(); PC && !PC->GetPawn())
		{
			ADefaultPawn* Pawn = World->SpawnActor<ADefaultPawn>(FVector(0.f, 0.f, 200.f), FRotator::ZeroRotator);
			if (Pawn) { PC->Possess(Pawn); }
		}
		if (!Terrain->GetPlanetData() && Terrain->GetActivePlanetName().IsEmpty())
		{
			Terrain->BeginLoadTerrain(TEXT("terrain/tatooine.trn"), FVector::ZeroVector);
		}
	}
	return BeginFromServer(TNumericLimits<uint64>::Max(), InTemplatePath);
}

void USWGStructurePlacementSubsystem::SetCandidate(FVector2D Raw)
{
	if (!IsActive()) { return; }
	Candidate = Raw;
	Validate();
	if (Preview) { Preview->UpdatePlacement(Candidate, Rotation, Footprint, ClearFloraRadius, bSnapToTerrain, Validation.Verdict); }
	OnPlacementUpdated.Broadcast();
}

void USWGStructurePlacementSubsystem::Rotate(int32 Steps)
{
	if (!IsActive()) { return; }
	Rotation = (Rotation + Steps % 4 + 4) % 4;
	Validate();
	if (Preview) { Preview->UpdatePlacement(Candidate, Rotation, Footprint, ClearFloraRadius, bSnapToTerrain, Validation.Verdict); }
	OnPlacementUpdated.Broadcast();
}

FString USWGStructurePlacementSubsystem::NeighbourName(const FSWGPlacementNeighbour& Neighbour) const
{
	const USWGTangibleComponent* Tangible = Neighbour.Actor.IsValid() ? Neighbour.Actor->FindComponentByClass<USWGTangibleComponent>() : nullptr;
	return Tangible ? Tangible->GetDisplayName() : Neighbour.Path;
}

void USWGStructurePlacementSubsystem::CollectNeighbours()
{
	Neighbours.Reset();
	// ponytail: linear scan of streamed actors; use a spatial index if cursor validation becomes slow in crowded cities.
	for (TActorIterator<ASWGObject> It(GetWorld()); It; ++It)
	{
		const ASWGObject* Actor = *It;
		const FString Path = !Actor->StaticTemplatePath.IsEmpty() ? Actor->StaticTemplatePath : Tre->ResolveTemplatePath(Actor->SWGObjectCRC);
		if (Path.IsEmpty()) { continue; }
		const FVector Raw = SWGToRawSpace(Actor->GetActorLocation());
		FSWGPlacementNeighbour Neighbour;
		Neighbour.Center = FVector2D(Raw.X, Raw.Y);
		Neighbour.DistanceSq = FVector2D::DistSquared(Candidate, Neighbour.Center);
		if (Neighbour.DistanceSq > FMath::Square(512.f)) { continue; }
		float* Radius = NoBuildRadii.Find(Path);
		if (!Radius)
		{
			float Value = 0.f;
			Meshes->ResolveTemplateFloatParam(Path, SWG_IFF_TAG('S','H','O','T'), TEXT("noBuildRadius"), Value);
			Radius = &NoBuildRadii.Add(Path, Value);
		}
		Neighbour.NoBuildRadius = *Radius;
		if (const FSWGStructureFootprint* OtherFootprint = FootprintForTemplate(Path))
		{
			const FBox2D Local = FSWGPlacementRules::GetServerFootprintRect(*OtherFootprint, FMath::RoundToInt(-Actor->GetActorRotation().Yaw));
			Neighbour.Rect = FBox2D(Local.Min + Neighbour.Center, Local.Max + Neighbour.Center);
			Neighbour.bHasRect = true;
		}
		if (Neighbour.NoBuildRadius <= 0.f && !Neighbour.bHasRect) { continue; }
		Neighbour.Actor = Actor;
		Neighbour.Path = Path;
		Neighbours.Add(MoveTemp(Neighbour));
	}
}

void USWGStructurePlacementSubsystem::Validate()
{
	Validation = {};
	Neighbours.Reset();
	Validation.ReasonStringId = TEXT("@system_msg:out_of_range");
	const AActor* Player = ObjectGraph->FindActor(ObjectGraph->GetLocalPlayerObjectId());
	if (!Player && bFake && GetWorld() && GetWorld()->GetFirstPlayerController()) { Player = GetWorld()->GetFirstPlayerController()->GetPawn(); }
	const auto Data = Terrain->GetPlanetData();
	if (!Player || !Data.IsValid()) { return; }
	CollectNeighbours();
	const FVector PlayerRaw = SWGToRawSpace(Player->GetActorLocation());
	const float Height = Terrain->GetHeightAt(Candidate.X, Candidate.Y);
	const FVector Point(Candidate.X, Candidate.Y, Height);
	if (FVector::Dist(PlayerRaw, Point) > 100.f || PlayerRaw.Z - Height > 10.f) { return; }
	Validation.ReasonStringId = TEXT("@player_structure:not_permitted");
	const float HalfMap = Data->Header.MapSize * .5f;
	if (FMath::Abs(Candidate.X) >= HalfMap || FMath::Abs(Candidate.Y) >= HalfMap) { return; }
	float WaterHeight = 0.f;
	if (Terrain->GetWaterHeightAt(Candidate.X, Candidate.Y, WaterHeight) && WaterHeight >= Height) { return; }
	for (const FVector2D& Poi : ClientPois)
	{
		if (FVector2D::DistSquared(Candidate, Poi) < FMath::Square(150.f)) { return; }
	}
	const FBox2D PlacingLocal = Footprint.IsValid() ? FSWGPlacementRules::GetServerFootprintRect(Footprint, Rotation * 90) : FBox2D();
	const FBox2D Placing(PlacingLocal.Min + Candidate, PlacingLocal.Max + Candidate);
	for (const FSWGPlacementNeighbour& Neighbour : Neighbours)
	{
		if (Neighbour.NoBuildRadius > 0.f && Neighbour.DistanceSq < FMath::Square(Neighbour.NoBuildRadius))
		{
			Validation.ReasonStringId = TEXT("@player_structure:city_too_close");
			Validation.ReasonParam = NeighbourName(Neighbour);
			return;
		}
	}
	// Every conflicting neighbour is flagged so the preview can outline it; the first one names the reason.
	FString BlockReason, BlockParam;
	for (FSWGPlacementNeighbour& Neighbour : Neighbours)
	{
		if (!Neighbour.bHasRect) { continue; }
		const bool bInside = FSWGPlacementRules::IsInStructureFootprint(Neighbour.Rect, Candidate);
		const bool bOverlap = !bInside && Footprint.IsValid() && Neighbour.DistanceSq <= FMath::Square(128.f)
			&& FSWGPlacementRules::RectsConflict(Placing, Neighbour.Rect);
		if (!bInside && !bOverlap) { continue; }
		Neighbour.bBlocking = true;
		if (BlockReason.IsEmpty())
		{
			BlockReason = bInside ? TEXT("@player_structure:city_too_close") : TEXT("@player_structure:no_room");
			BlockParam = bInside ? NeighbourName(Neighbour) : FString();
		}
	}
	if (!BlockReason.IsEmpty())
	{
		Validation.ReasonStringId = BlockReason;
		Validation.ReasonParam = BlockParam;
		return;
	}
	if (!bPlanetKnown)
	{
		Validation.Verdict = ESWGPlacementVerdict::Uncertain;
		Validation.ReasonStringId = TEXT("Checking deed planets");
		return;
	}
	if (!bPlanetAllowed)
	{
		Validation.ReasonStringId = TEXT("@player_structure:wrong_planet");
		return;
	}
	if (!bRuleDataReady)
	{
		Validation.Verdict = ESWGPlacementVerdict::Uncertain;
		Validation.ReasonStringId = TEXT("Placement data unavailable");
		return;
	}
	for (const FCityCircle& City : StaticCities)
	{
		if (FVector2D::Distance(Candidate, City.Center) < City.Radius) { return; }
	}
	for (const FCityCircle& City : StaticCities)
	{
		const float Distance = FVector2D::Distance(Candidate, City.Center);
		if (Distance < City.Radius + 500.f)
		{
			Validation.Verdict = ESWGPlacementVerdict::Uncertain;
			Validation.ReasonStringId = TEXT("Near city — server may refuse");
			Validation.ReasonParam = City.Name;
			return;
		}
	}
	Validation.Verdict = ESWGPlacementVerdict::Valid;
	Validation.ReasonStringId.Empty();
}

void USWGStructurePlacementSubsystem::LoadPlanetRules()
{
	const FString Planet = Terrain->GetActivePlanetName().ToLower();
	if (Planet == RulesPlanet) { return; }
	RulesPlanet = Planet;
	ClientPois.Reset();
	StaticCities.Reset();
	NoBuildRadii.Reset();
	FootprintsByTemplate.Reset();
	bRuleDataReady = false;
	FSWGDataTableData Table;
	const bool bPoisLoaded = FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(TEXT("datatables/clientpoi/clientpoi.iff")), Table);
	if (bPoisLoaded)
	{
		for (int32 Row = 0; Row < Table.Rows.Num(); ++Row)
		{
			if (Table.GetCell(Row, TEXT("Planet")).Equals(Planet, ESearchCase::IgnoreCase))
			{
				// Core3 ClientPoiDataTable::readObject: column 6 (Z) is world north.
				ClientPois.Emplace(FCString::Atof(*Table.GetCell(Row, TEXT("X"))), FCString::Atof(*Table.GetCell(Row, TEXT("Z"))));
			}
		}
	}
	Table = {};
	// Zones without a clientregion table (e.g. the creature_test sandbox) simply have no static cities.
	const FString CityTablePath = FString::Printf(TEXT("datatables/clientregion/%s.iff"), *Planet);
	const bool bCitiesLoaded = !Tre->FileExists(CityTablePath) || FSWGDataTableReader::ReadDataTable(Tre->CreateIffReader(CityTablePath), Table);
	if (bCitiesLoaded)
	{
		for (int32 Row = 0; Row < Table.Rows.Num(); ++Row)
		{
			FCityCircle& City = StaticCities.AddDefaulted_GetRef();
			City.Center = FVector2D(FCString::Atof(*Table.GetCell(Row, TEXT("X"))), FCString::Atof(*Table.GetCell(Row, TEXT("Z"))));
			City.Radius = FCString::Atof(*Table.GetCell(Row, TEXT("Radius")));
			City.Name = Tre->ResolveStringId(Table.GetCell(Row, TEXT("Name")));
		}
	}
	bRuleDataReady = bPoisLoaded && bCitiesLoaded;
}

const FSWGStructureFootprint* USWGStructurePlacementSubsystem::FootprintForTemplate(const FString& Path)
{
	if (FSWGStructureFootprint* Cached = FootprintsByTemplate.Find(Path)) { return Cached->IsValid() ? Cached : nullptr; }
	FSWGStructureFootprint& Cached = FootprintsByTemplate.Add(Path);
	FString FootprintPath;
	if (Meshes->ResolveTemplateStringParam(Path, SWG_IFF_TAG('S','T','O','T'), TEXT("structureFootprintFileName"), FootprintPath)
		&& !FootprintPath.IsEmpty()) { FSWGFootprintReader::Read(Tre->CreateIffReader(FootprintPath), Cached); }
	return Cached.IsValid() ? &Cached : nullptr;
}

bool USWGStructurePlacementSubsystem::Confirm()
{
	return ConfirmWithProjector(nullptr);
}

bool USWGStructurePlacementSubsystem::ConfirmWithProjector(AActor* Projector)
{
	if (!IsActive() || bFake || Validation.Verdict != ESWGPlacementVerdict::Valid) { return false; }
	const FString Args = FString::Printf(TEXT("%llu %.2f %.2f %d"), DeedId, Candidate.X, Candidate.Y, Rotation);
	if (!Commands || !Commands->SendCommand(TEXT("placestructure"), 0, Args)) { return false; }
	// Keep the hologram at the site until the server's finished structure replaces it.
	if (Preview)
	{
		FPendingMarker& Pending = PendingMarkers.AddDefaulted_GetRef();
		Pending.Marker = Preview;
		Pending.Projector = Projector;
		Pending.Center = Candidate;
		Pending.TemplatePath = TemplatePath;
		Preview->SetHologramOnly(true);
		Preview->SetPresentationHidden(false);
		Preview = nullptr;
	}
	Cancel();
	return true;
}

void USWGStructurePlacementSubsystem::Cancel()
{
	if (!IsActive()) { return; }
	DeedId = 0;
	bFake = false;
	bPlanetKnown = false;
	bPlanetAllowed = false;
	TemplatePath.Empty();
	Footprint = {};
	if (Preview) { Preview->Destroy(); Preview = nullptr; }
	OnPlacementEnded.Broadcast();
}

void USWGStructurePlacementSubsystem::HandleObjectDestroyed(int64 ObjectId)
{
	if (TWeakObjectPtr<ASWGStructurePlacementPreview>* Outline = ConstructionOutlines.Find(ObjectId))
	{
		if (Outline->IsValid()) { (*Outline)->Destroy(); }
		ConstructionOutlines.Remove(ObjectId);
	}
	if (!bFake && ObjectId == static_cast<int64>(DeedId)) { Cancel(); }
}

void USWGStructurePlacementSubsystem::ClearPendingMarkers()
{
	for (FPendingMarker& Pending : PendingMarkers)
	{
		if (Pending.Marker.IsValid()) { Pending.Marker->Destroy(); }
		if (Pending.Projector.IsValid()) { Pending.Projector->Destroy(); }
		if (Pending.Barricade.IsValid()) { Pending.Barricade->SetActorHiddenInGame(false); }
	}
	PendingMarkers.Reset();
	for (const TPair<int64, TWeakObjectPtr<ASWGStructurePlacementPreview>>& Outline : ConstructionOutlines)
	{
		if (Outline.Value.IsValid()) { Outline.Value->Destroy(); }
	}
	ConstructionOutlines.Reset();
}

void USWGStructurePlacementSubsystem::TickPendingMarkers(float DeltaTime)
{
	// Core3 spawns a construction barricade first, then the real structure at the same spot after lots * 3 s.
	// Without a barricade shortly after confirming, the server refused the placement (its chat message says why).
	constexpr float RefusedAfterSeconds = 8.f, GiveUpAfterSeconds = 600.f, PollSeconds = .5f, MatchRadius = 10.f;
	for (int32 Index = PendingMarkers.Num() - 1; Index >= 0; --Index)
	{
		FPendingMarker& Pending = PendingMarkers[Index];
		Pending.Elapsed += DeltaTime;
		Pending.PollTimer += DeltaTime;
		if (Pending.bConstructionSeen && Pending.Marker.IsValid())
		{
			Pending.PrintElapsed += DeltaTime;
			Pending.Marker->SetConstructionProgress(FMath::Min(.99f, Pending.PrintElapsed / Pending.PrintDuration));
		}
		bool bDone = !Pending.Marker.IsValid() || Pending.Elapsed > GiveUpAfterSeconds;
		if (!bDone && Pending.PollTimer >= PollSeconds)
		{
			Pending.PollTimer = 0.f;
			for (TActorIterator<ASWGObject> It(GetWorld()); It && !bDone; ++It)
			{
				ASWGObject* Actor = *It;
				const FVector Raw = SWGToRawSpace(Actor->GetActorLocation());
				if (FVector2D::DistSquared(Pending.Center, FVector2D(Raw.X, Raw.Y)) > FMath::Square(MatchRadius)) { continue; }
				const FString Path = !Actor->StaticTemplatePath.IsEmpty() ? Actor->StaticTemplatePath : Tre->ResolveTemplatePath(Actor->SWGObjectCRC);
				if (Path.Contains(TEXT("construction")))
				{
					if (!Pending.bConstructionSeen && Pending.Marker.IsValid()) { Pending.Marker->BeginConstructionPrint(); }
					Pending.bConstructionSeen = true;
					Pending.Barricade = Actor;
					Actor->SetActorHiddenInGame(true);
				}
				else if (Path.Equals(Pending.TemplatePath, ESearchCase::IgnoreCase)) { bDone = true; }
			}
			bDone = bDone || (!Pending.bConstructionSeen && Pending.Elapsed > RefusedAfterSeconds);
			// The local construction hologram replaces the barricade's yellow outline.
			if (Pending.bConstructionSeen)
			{
				for (const TPair<int64, TWeakObjectPtr<ASWGStructurePlacementPreview>>& Outline : ConstructionOutlines)
				{
					if (Outline.Value.IsValid() && FVector2D::DistSquared(Outline.Value->GetCenter(), Pending.Center) < FMath::Square(MatchRadius)) { Outline.Value->SetPresentationHidden(true); }
				}
			}
		}
		if (bDone)
		{
			if (Pending.Marker.IsValid()) { Pending.Marker->Destroy(); }
			if (Pending.Projector.IsValid()) { Pending.Projector->Destroy(); }
			if (Pending.Barricade.IsValid()) { Pending.Barricade->SetActorHiddenInGame(false); }
			PendingMarkers.RemoveAtSwap(Index);
		}
	}
}

void USWGStructurePlacementSubsystem::HandleObjectReady(int64 ObjectId)
{
	UWorld* World = GetWorld();
	const ASWGObject* Actor = Cast<ASWGObject>(ObjectGraph->FindActor(ObjectId));
	if (!World || !Actor || ConstructionOutlines.Contains(ObjectId)) { return; }
	const FString Path = !Actor->StaticTemplatePath.IsEmpty() ? Actor->StaticTemplatePath : Tre->ResolveTemplatePath(Actor->SWGObjectCRC);
	bool bUsesOutline = false;
	if (Path.IsEmpty() || !Meshes->ResolveTemplateBoolParam(Path, SWG_IFF_TAG('S','T','O','T'), TEXT("useStructureFootprintOutline"), bUsesOutline) || !bUsesOutline) { return; }
	const FSWGStructureFootprint* Outline = FootprintForTemplate(Path);
	if (!Outline) { return; }
	// Construction barricades carry the finished building's footprint; the server only rotates in 90 degree steps.
	const FVector Raw = SWGToRawSpace(Actor->GetActorLocation());
	const int32 Steps = ((FMath::RoundToInt(-Actor->GetActorRotation().Yaw / 90.f) % 4) + 4) % 4;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	ASWGStructurePlacementPreview* Site = World->SpawnActor<ASWGStructurePlacementPreview>(Params);
	if (!Site) { return; }
	Site->Initialize(FString(), Meshes, Terrain, ObjectGraph, this);
	Site->UpdatePlacement(FVector2D(Raw.X, Raw.Y), Steps, *Outline, 0.f, false, ESWGPlacementVerdict::Valid);
	Site->ShowAsMarker();
	for (const FPendingMarker& Pending : PendingMarkers)
	{
		if (Pending.bConstructionSeen && FVector2D::DistSquared(Pending.Center, Site->GetCenter()) < FMath::Square(10.f)) { Site->SetPresentationHidden(true); break; }
	}
	ConstructionOutlines.Add(ObjectId, Site);
}

void USWGStructurePlacementSubsystem::SetPreviewHidden(bool bHidden)
{
	if (Preview) { Preview->SetPresentationHidden(bHidden); }
}
