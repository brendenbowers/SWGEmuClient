#include "Subsystems/SWGAmbienceSubsystem.h"

#include "Audio/SWGInteriorTable.h"
#include "Audio/SWGReverbPresets.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"
#include "Objects/World/SWGSoundEmitter.h"
#include "SWGAudioSubsystem.h"
#include "SWGAudioTypes.h"
#include "SWGMusicManager.h"
#include "Sound/ReverbEffect.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGWeatherSubsystem.h"
#include "TRE/SWGClientDataFileReader.h"
#include "TRE/SWGDataTableReader.h"

namespace
{
	constexpr float TickSeconds = 0.25f;
	constexpr double OutdoorSampleSeconds = 1.0;
	constexpr double EmitterUpdateSeconds = 0.5;

	const FName ReverbTag(TEXT("SWGInterior"));

	TAutoConsoleVariable<float> CVarCrossfadeSeconds(
		TEXT("swg.Audio.AmbienceCrossfade"), 4.f,
		TEXT("Seconds an ambient loop takes to fade out while its replacement fades in (day/night, family and room changes)."));

	TAutoConsoleVariable<float> CVarMusicRepeatSeconds(
		TEXT("swg.Audio.MusicRepeatSeconds"), 600.f,
		TEXT("Minimum seconds before a family's or room's First Music plays again."));

	TAutoConsoleVariable<int32> CVarMaxEmitterLoops(
		TEXT("swg.Audio.MaxEmitterLoops"), 16,
		TEXT("Most sound-object loops kept playing at once; the nearest emitters win."));

	TAutoConsoleVariable<float> CVarReverbVolume(
		TEXT("swg.Audio.ReverbVolume"), 0.5f,
		TEXT("Reverb send level of the interior room preset (0 disables the effect)."));
}

void USWGAmbienceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Audio = Cast<USWGAudioSubsystem>(Collection.InitializeDependency(USWGAudioSubsystem::StaticClass()));
	Tre = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));
	Terrain = Cast<USWGTerrainSubsystem>(Collection.InitializeDependency(USWGTerrainSubsystem::StaticClass()));
	MeshGenerator = Cast<USWGMeshGeneratorSubsystem>(Collection.InitializeDependency(USWGMeshGeneratorSubsystem::StaticClass()));
	Weather = Cast<USWGWeatherSubsystem>(Collection.InitializeDependency(USWGWeatherSubsystem::StaticClass()));

	RoomChangedHandle = ASWGBuilding::OnLocalPlayerRoomChanged.AddUObject(this, &USWGAmbienceSubsystem::HandleRoomChanged);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USWGAmbienceSubsystem::Tick), TickSeconds);
}

void USWGAmbienceSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	ASWGBuilding::OnLocalPlayerRoomChanged.Remove(RoomChangedHandle);

	StopSlots(0.f);
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		ClearReverb(GameInstance->GetWorld());
	}
	Emitters.Reset();
	Audio = nullptr;
	Tre = nullptr;
	Terrain = nullptr;
	MeshGenerator = nullptr;
	Weather = nullptr;
	Super::Deinitialize();
}

bool USWGAmbienceSubsystem::LoadEnvironmentTable(const USWGTreSubsystem& Tre, const FString& Planet, TArray<FSWGEnvironmentRow>& OutRows)
{
	OutRows.Reset();
	const FString TablePath = FString::Printf(TEXT("datatables/environment/%s.iff"), *Planet);
	FSWGDataTableData Table;
	if (!Tre.FileExists(TablePath) || !FSWGDataTableReader::ReadDataTable(Tre.CreateIffReader(TablePath), Table))
	{
		return false;
	}

	for (int32 RowIndex = 0; RowIndex < Table.Rows.Num(); ++RowIndex)
	{
		FSWGEnvironmentRow& Row = OutRows.AddDefaulted_GetRef();
		Row.Family = Table.GetCell(RowIndex, TEXT("Environment Family Name"));
		Row.WeatherIndex = FCString::Atoi(*Table.GetCell(RowIndex, TEXT("Weather Index")));
		auto PathCell = [&](const TCHAR* Column) { return Table.GetCell(RowIndex, Column).Replace(TEXT("\\"), TEXT("/")); };
		Row.GradientSky = PathCell(TEXT("Gradient Sky Texture (256x256 dds)"));
		Row.ColorRamp = PathCell(TEXT("Lighting Color Ramps (256x8 tga)"));
		Row.CloudBottomShader = PathCell(TEXT("Cloud Layer Bottom Shader (sht)"));
		Row.CloudBottomSize = FCString::Atof(*Table.GetCell(RowIndex, TEXT("Cloud Layer Bottom Shader Size")));
		Row.CloudBottomSpeed = FCString::Atof(*Table.GetCell(RowIndex, TEXT("Cloud Layer Bottom Speed")));
		Row.CloudTopShader = PathCell(TEXT("Cloud Layer Top Shader (sht)"));
		Row.CloudTopSize = FCString::Atof(*Table.GetCell(RowIndex, TEXT("Cloud Layer Top Shader Size")));
		Row.CloudTopSpeed = FCString::Atof(*Table.GetCell(RowIndex, TEXT("Cloud Layer Top Speed")));
		Row.DayEnvironmentMap = PathCell(TEXT("Day Environment Map (dds)"));
		Row.NightEnvironmentMap = PathCell(TEXT("Night Environment Map (dds)"));
		Row.bFogEnabled = FCString::Atoi(*Table.GetCell(RowIndex, TEXT("Fog Enabled"))) != 0;
		Row.MinFogDensity = FCString::Atof(*Table.GetCell(RowIndex, TEXT("Minimum Fog Density")));
		Row.MaxFogDensity = FCString::Atof(*Table.GetCell(RowIndex, TEXT("Maximum Fog Density")));
		Row.CameraParticle = PathCell(TEXT("Attached Camera AppearanceTemplate"));
		Row.WindSpeedScale = FCString::Atof(*Table.GetCell(RowIndex, TEXT("Wind Speed Scale")));
		Row.DayAmbient[0] = Table.GetCell(RowIndex, TEXT("Day Ambient Sound 1 (2d .snd)"));
		Row.DayAmbient[1] = Table.GetCell(RowIndex, TEXT("Day Ambient Sound 2 (2d .snd)"));
		Row.NightAmbient[0] = Table.GetCell(RowIndex, TEXT("Night Ambient Sound 1 (2d .snd)"));
		Row.NightAmbient[1] = Table.GetCell(RowIndex, TEXT("Night Ambient Sound 2 (2d .snd)"));
		Row.FirstMusic = Table.GetCell(RowIndex, TEXT("First Music (2d .snd)"));
		Row.SunriseMusic = Table.GetCell(RowIndex, TEXT("Sunrise Music (2d .snd)"));
		Row.SunsetMusic = Table.GetCell(RowIndex, TEXT("Sunset Music (2d .snd)"));
	}
	return !OutRows.IsEmpty();
}

const FSWGEnvironmentRow* USWGAmbienceSubsystem::FindEnvironmentRow(const TArray<FSWGEnvironmentRow>& Rows, const FString& FamilyName, int32 InWeatherIndex)
{
	auto Find = [&Rows](const FString& Name, int32 Weather) -> const FSWGEnvironmentRow*
	{
		return Rows.FindByPredicate([&](const FSWGEnvironmentRow& Row)
			{
				return Row.Family.Equals(Name, ESearchCase::IgnoreCase) && Row.WeatherIndex == Weather;
			});
	};

	for (const FString& Name : { FamilyName, FString(TEXT("global")) })
	{
		const FSWGEnvironmentRow* Row = Find(Name, InWeatherIndex);
		// A family with fewer weathers (09, taanab have four) uses its worst one.
		for (int32 Lower = InWeatherIndex - 1; !Row && Lower >= 0; --Lower)
		{
			Row = Find(Name, Lower);
		}
		if (Row)
		{
			return Row;
		}
	}

	// Only Tatooine and a few others name their base family "global" (Naboo's is main_base, 09's default).
	return Rows.FindByPredicate([](const FSWGEnvironmentRow& Row) { return Row.WeatherIndex == 0; });
}

// -- Emitters ---------------------------------------------------------------

void USWGAmbienceSubsystem::RegisterEmitter(ASWGSoundEmitter* Emitter)
{
	Emitters.AddUnique(Emitter);
}

void USWGAmbienceSubsystem::UnregisterEmitter(ASWGSoundEmitter* Emitter)
{
	Emitters.Remove(Emitter);
}

const TArray<FString>& USWGAmbienceSubsystem::ResolveSoundObjectLoops(const FString& TemplatePath)
{
	if (const TArray<FString>* Cached = SoundObjectLoops.Find(TemplatePath))
	{
		return *Cached;
	}

	TArray<FString>& Loops = SoundObjectLoops.Add(TemplatePath);
	FSWGClientDataFile ClientData;
	if (MeshGenerator && Tre && MeshGenerator->ReadClientDataFile(TemplatePath, ClientData))
	{
		for (const FString& Path : ClientData.AttachedSounds)
		{
			if (Tre->FileExists(SWGAudio::NormalizeSoundPath(Path)))
			{
				Loops.Add(Path);
			}
			else
			{
				UE_LOG(LogSWGAudio, Verbose, TEXT("sound object %s: %s does not exist"), *TemplatePath, *Path);
			}
		}
	}
	return Loops;
}

void USWGAmbienceSubsystem::UpdateEmitters(const FVector& ListenerLocation)
{
	Emitters.RemoveAll([](const TWeakObjectPtr<ASWGSoundEmitter>& Emitter) { return !Emitter.IsValid(); });

	struct FRanked
	{
		float Distance;
		ASWGSoundEmitter* Emitter;
	};
	TArray<FRanked> Ranked;
	Ranked.Reserve(Emitters.Num());
	for (const TWeakObjectPtr<ASWGSoundEmitter>& Weak : Emitters)
	{
		if (ASWGSoundEmitter* Emitter = Weak.Get())
		{
			Ranked.Add({ static_cast<float>(FVector::Dist(ListenerLocation, Emitter->GetActorLocation())), Emitter });
		}
	}
	Ranked.Sort([](const FRanked& Left, const FRanked& Right) { return Left.Distance < Right.Distance; });

	// Nearest first: once the loop budget is used up, the farther emitters stop theirs.
	const int32 MaxLoops = FMath::Max(0, CVarMaxEmitterLoops.GetValueOnGameThread());
	int32 ActiveLoops = 0;
	for (const FRanked& Entry : Ranked)
	{
		Entry.Emitter->UpdateLoops(ListenerLocation, ActiveLoops < MaxLoops);
		ActiveLoops += Entry.Emitter->GetActiveLoopCount();
	}
	ActiveEmitterLoops = ActiveLoops;
}

// -- Tick -------------------------------------------------------------------

bool USWGAmbienceSubsystem::Tick(float DeltaSeconds)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	if (!Controller || !Audio || !Terrain || !Terrain->IsTerrainLoaded())
	{
		return true;
	}

	const FString Planet = Terrain->GetActivePlanetName();
	if (Planet != PlanetName)
	{
		ResetPlanet(Planet);
	}

	const double Now = FPlatformTime::Seconds();
	if (Now - LastEmitterUpdate >= EmitterUpdateSeconds)
	{
		LastEmitterUpdate = Now;
		UpdateEmitters(Controller->GetFocalLocation());
	}

	UpdateRoom(World);

	if (!InteriorRow && Now - LastOutdoorSample >= OutdoorSampleSeconds)
	{
		LastOutdoorSample = Now;
		const APawn* Pawn = Controller->GetPawn();
		UpdateOutdoorFamily(Pawn ? Pawn->GetActorLocation() : Controller->GetFocalLocation());
	}

	const bool bNowDay = Terrain->IsDaytime();
	if (bDayKnown && bNowDay != bDay && !InteriorRow && EnvironmentRow)
	{
		// Sunrise and sunset themes are one-shots over the Environment layer.
		if (USWGMusicManager* Music = Audio->GetMusicManager())
		{
			Music->SetLayer(ESWGMusicLayer::Environment, bNowDay ? EnvironmentRow->SunriseMusic : EnvironmentRow->SunsetMusic);
		}
	}
	bDay = bNowDay;
	bDayKnown = true;

	UpdateAmbientSlots(bDay);
	return true;
}

void USWGAmbienceSubsystem::ResetPlanet(const FString& NewPlanet)
{
	StopSlots(CVarCrossfadeSeconds.GetValueOnGameThread());
	PlanetName = NewPlanet;
	Family.Reset();
	EnvironmentRow = nullptr;
	bDayKnown = false;
	MusicLastPlayed.Reset();
	LastOutdoorSample = 0.0;

	if (Tre && !LoadEnvironmentTable(*Tre, PlanetName, EnvironmentRows))
	{
		EnvironmentRows.Reset();
		UE_LOG(LogSWGAudio, Log, TEXT("ambience: no environment table for %s"), *PlanetName);
	}
}

void USWGAmbienceSubsystem::UpdateOutdoorFamily(const FVector& PlayerLocation)
{
	const FString NewFamily = Terrain->GetEnvironmentFamilyAt(PlayerLocation);
	if (NewFamily.IsEmpty())
	{
		return;
	}

	const bool bChanged = NewFamily != Family;
	Family = NewFamily;
	EnvironmentRow = FindEnvironmentRow(EnvironmentRows, Family, Weather ? Weather->GetWeatherIndex() : 0);
	if (bChanged && EnvironmentRow)
	{
		UE_LOG(LogSWGAudio, Log, TEXT("ambience: environment family %s on %s"), *Family, *PlanetName);
		PlayMusicOnce(PlanetName + TEXT("/") + Family, EnvironmentRow->FirstMusic);
	}
}

void USWGAmbienceSubsystem::HandleRoomChanged(ASWGCell* Cell)
{
	PendingCell = Cell;
	bRoomDirty = true;
}

void USWGAmbienceSubsystem::UpdateRoom(UWorld* World)
{
	if (InteriorRow && !CurrentCell.IsValid())
	{
		// The room was streamed out under the player.
		PendingCell.Reset();
		bRoomDirty = true;
	}
	if (!bRoomDirty)
	{
		return;
	}
	bRoomDirty = false;

	ASWGCell* Cell = PendingCell.Get();
	if (Cell == CurrentCell.Get())
	{
		return;
	}
	CurrentCell = Cell;

	if (!Cell)
	{
		InteriorRow = nullptr;
		InteriorMatchName.Reset();
		ClearReverb(World);
		LastOutdoorSample = 0.0;
		return;
	}

	FSWGInteriorTable::EMatch Match = FSWGInteriorTable::EMatch::None;
	const FSWGInteriorRow* Row = FSWGInteriorTable::FindForCell(Cell, *GetGameInstance(), &Match);
	if (!Row && Tre)
	{
		// Building not resolved yet: the table's global default still describes "inside".
		const FSWGInteriorTable* Table = FSWGInteriorTable::Get(*Tre);
		Row = Table ? Table->Find(FString(), FString(), &Match) : nullptr;
	}
	InteriorRow = Row;
	InteriorMatchName = FSWGInteriorTable::MatchName(Match);
	if (!Row)
	{
		return;
	}

	UE_LOG(LogSWGAudio, Log, TEXT("ambience: entered %s/%s (%s), room type %s"), *Row->PobName, *Row->CellName, *InteriorMatchName, SWGReverbPresets::GetRoomTypeName(Row->RoomType));
	SetReverb(World, Row->RoomType);
	PlayMusicOnce(Row->PobName + TEXT("/") + Row->CellName, Row->FirstMusic);
}

// -- Ambient loops and music ------------------------------------------------

void USWGAmbienceSubsystem::UpdateAmbientSlots(bool bDaytime)
{
	FString Wanted[2];
	if (InteriorRow)
	{
		Wanted[0] = bDaytime ? InteriorRow->DayAmbient : InteriorRow->NightAmbient;
	}
	else if (EnvironmentRow)
	{
		for (int32 SlotIndex = 0; SlotIndex < 2; ++SlotIndex)
		{
			Wanted[SlotIndex] = bDaytime ? EnvironmentRow->DayAmbient[SlotIndex] : EnvironmentRow->NightAmbient[SlotIndex];
		}
	}

	for (int32 SlotIndex = 0; SlotIndex < 2; ++SlotIndex)
	{
		SetSlotPath(SlotIndex, Wanted[SlotIndex]);
	}
}

void USWGAmbienceSubsystem::SetSlotPath(int32 SlotIndex, const FString& Path)
{
	FSlot& Slot = Slots[SlotIndex];
	if (Slot.Path == Path)
	{
		return;
	}

	const float Crossfade = CVarCrossfadeSeconds.GetValueOnGameThread();
	Slot.Handle.Stop(Crossfade);
	Slot.Handle = FSWGSoundHandle();
	Slot.Path = Path;
	if (Path.IsEmpty() || !Audio)
	{
		return;
	}
	if (Tre && !Tre->FileExists(SWGAudio::NormalizeSoundPath(Path)))
	{
		UE_LOG(LogSWGAudio, Warning, TEXT("ambience: %s does not exist"), *Path);
		return;
	}

	FSWGSoundPlayOptions Options;
	Options.FadeInSeconds = Crossfade;
	Slot.Handle = Audio->PlaySound2D(Path, Options);
}

void USWGAmbienceSubsystem::StopSlots(float FadeSeconds)
{
	for (FSlot& Slot : Slots)
	{
		Slot.Handle.Stop(FadeSeconds);
		Slot = FSlot();
	}
}

void USWGAmbienceSubsystem::PlayMusicOnce(const FString& Key, const FString& SndPath)
{
	USWGMusicManager* Music = Audio ? Audio->GetMusicManager() : nullptr;
	if (!Music || USWGMusicManager::IsSilence(SndPath))
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const double* Last = MusicLastPlayed.Find(Key);
	if (Last && Now - *Last < CVarMusicRepeatSeconds.GetValueOnGameThread())
	{
		return;
	}
	MusicLastPlayed.Add(Key, Now);
	Music->SetLayer(ESWGMusicLayer::Environment, SndPath);
}

// -- Reverb -----------------------------------------------------------------

void USWGAmbienceSubsystem::SetReverb(UWorld* World, int32 RoomType)
{
	const float Volume = CVarReverbVolume.GetValueOnGameThread();
	if (!World || RoomType == ReverbRoomType || Volume <= 0.f)
	{
		return;
	}

	TObjectPtr<UReverbEffect>& Effect = ReverbEffects.FindOrAdd(RoomType);
	if (!Effect)
	{
		Effect = SWGReverbPresets::GetReverbEffect(RoomType, this);
	}
	if (Effect)
	{
		UGameplayStatics::ActivateReverbEffect(World, Effect, ReverbTag, 1.f, Volume, 1.f);
		ReverbRoomType = RoomType;
	}
}

void USWGAmbienceSubsystem::ClearReverb(UWorld* World)
{
	if (World && ReverbRoomType != INDEX_NONE)
	{
		UGameplayStatics::DeactivateReverbEffect(World, ReverbTag);
	}
	ReverbRoomType = INDEX_NONE;
}

// -- Diagnostics ------------------------------------------------------------

TArray<FString> USWGAmbienceSubsystem::DescribeState() const
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("planet %s, %s, %d environment row(s)"), *PlanetName, bDay ? TEXT("day") : TEXT("night"), EnvironmentRows.Num()));
	Lines.Add(FString::Printf(TEXT("environment family %s -> row %s (weather %d)"), *Family,
		EnvironmentRow ? *EnvironmentRow->Family : TEXT("none"), EnvironmentRow ? EnvironmentRow->WeatherIndex : 0));
	if (InteriorRow)
	{
		Lines.Add(FString::Printf(TEXT("interior row %s / %s (%s), surface %s, room type %d %s"), *InteriorRow->PobName, *InteriorRow->CellName,
			*InteriorMatchName, *InteriorRow->SurfaceType, InteriorRow->RoomType, SWGReverbPresets::GetRoomTypeName(InteriorRow->RoomType)));
	}
	else
	{
		Lines.Add(TEXT("interior: none (outdoors)"));
	}
	Lines.Add(FString::Printf(TEXT("reverb %s"), ReverbRoomType == INDEX_NONE ? TEXT("off") : SWGReverbPresets::GetRoomTypeName(ReverbRoomType)));
	for (int32 SlotIndex = 0; SlotIndex < 2; ++SlotIndex)
	{
		Lines.Add(FString::Printf(TEXT("ambient slot %d: %s %s"), SlotIndex + 1, Slots[SlotIndex].Path.IsEmpty() ? TEXT("(none)") : *Slots[SlotIndex].Path,
			Slots[SlotIndex].Handle.IsPlaying() ? TEXT("PLAYING") : TEXT("")));
	}
	if (const USWGMusicManager* Music = Audio ? Audio->GetMusicManager() : nullptr)
	{
		const FString& LayerPath = Music->GetLayerPath(ESWGMusicLayer::Environment);
		Lines.Add(FString::Printf(TEXT("environment music layer: %s"), LayerPath.IsEmpty() ? TEXT("(empty)") : *LayerPath));
	}
	Lines.Add(FString::Printf(TEXT("%d sound emitter(s), %d loop(s) playing"), Emitters.Num(), ActiveEmitterLoops));
	return Lines;
}
