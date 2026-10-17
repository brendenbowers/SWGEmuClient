#include "Subsystems/SWGWeatherSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/ServerWeatherMessage.h"
#include "Objects/World/SWGBuilding.h"
#include "Objects/World/SWGCell.h"
#include "SWGEffectsSubsystem.h"
#include "Subsystems/SWGAmbienceSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGPropAttachmentsSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	constexpr double FamilySampleSeconds = 1.0;
	constexpr double ParticleCheckSeconds = 1.0;

	// The largest Wind Speed Scale in the environment tables (Tatooine's heavy storm); full flag strength.
	constexpr float StrongestWindScale = 15.f;

	TAutoConsoleVariable<float> CVarBlendSeconds(
		TEXT("swg.Weather.BlendSeconds"), 6.f,
		TEXT("Seconds a weather change takes to blend fog, lighting, sky, clouds and wind (0 is instant)."));

	TAutoConsoleVariable<int32> CVarParticles(
		TEXT("swg.Weather.Particles"), 1,
		TEXT("0 hides the camera weather particle (rain sheets, dust storms, fog, leaves)."));
}

void USWGWeatherSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Tre = Cast<USWGTreSubsystem>(Collection.InitializeDependency(USWGTreSubsystem::StaticClass()));
	Terrain = Cast<USWGTerrainSubsystem>(Collection.InitializeDependency(USWGTerrainSubsystem::StaticClass()));
	Network = Cast<USWGNetworkSubsystem>(Collection.InitializeDependency(USWGNetworkSubsystem::StaticClass()));

	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGWeatherSubsystem::HandleMessageReceived);
	}
	RoomChangedHandle = ASWGBuilding::OnLocalPlayerRoomChanged.AddUObject(this, &USWGWeatherSubsystem::HandleRoomChanged);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USWGWeatherSubsystem::Tick), 0.f);
}

void USWGWeatherSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	ASWGBuilding::OnLocalPlayerRoomChanged.Remove(RoomChangedHandle);
	if (Network && MessageHandle.IsValid())
	{
		Network->OnMessageReceived.Remove(MessageHandle);
		MessageHandle.Reset();
	}

	StopParticle();
	if (IsValid(ParticleAnchor))
	{
		ParticleAnchor->Destroy();
	}
	ParticleAnchor = nullptr;
	TargetRow = nullptr;
	Tre = nullptr;
	Terrain = nullptr;
	Network = nullptr;
	Super::Deinitialize();
}

FVector USWGWeatherSubsystem::ComputeWindSpeed(const FVector& InWindVector, float WindSpeedScale)
{
	// Core3 fills the vertical component with a random value; a real updraft would only tilt the rain, so it is dropped.
	return FVector(InWindVector.X, 0.0, InWindVector.Z) * WindSpeedScale;
}

float USWGWeatherSubsystem::ComputeWindStrength(const FVector& WindSpeed)
{
	return FMath::Clamp(static_cast<float>(FVector2D(WindSpeed.X, WindSpeed.Z).Size()) / StrongestWindScale, 0.f, 1.f);
}

void USWGWeatherSubsystem::SetWeather(int32 NewWeatherIndex, const FVector& NewWindVector)
{
	WeatherIndex = FMath::Clamp(NewWeatherIndex, 0, 4);
	WindVector = NewWindVector;
	bHasInput = true;
	UE_LOG(LogTemp, Log, TEXT("USWGWeatherSubsystem: weather %d, wind (%.2f, %.2f, %.2f)"), WeatherIndex, WindVector.X, WindVector.Y, WindVector.Z);
}

void USWGWeatherSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message)
{
	if (Message && Message->Opcode == static_cast<uint32>(ESWGMessageOp::ServerWeatherMessage))
	{
		const FServerWeatherMessage* Weather = static_cast<const FServerWeatherMessage*>(Message.Get());
		SetWeather(Weather->Weather, FVector(Weather->WindX, Weather->WindUp, Weather->WindZ));
	}
}

void USWGWeatherSubsystem::HandleRoomChanged(ASWGCell* Cell)
{
	CurrentCell = Cell;
}

void USWGWeatherSubsystem::ResetPlanet(const FString& NewPlanetName)
{
	StopParticle();
	PlanetName = NewPlanetName;
	TargetRow = nullptr;
	Family.Reset();
	if (!Tre || !USWGAmbienceSubsystem::LoadEnvironmentTable(*Tre, PlanetName, Rows))
	{
		Rows.Reset();
	}
}

bool USWGWeatherSubsystem::Tick(float DeltaSeconds)
{
	if (Terrain && Terrain->IsSpaceScene())
	{
		StopParticle();
		TargetRow = nullptr;
		return true;
	}
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	if (!Controller || !Tre || !Terrain || !Terrain->IsTerrainLoaded())
	{
		return true;
	}

	const FString Planet = Terrain->GetActivePlanetName();
	if (Planet != PlanetName)
	{
		ResetPlanet(Planet);
	}
	if (Terrain->GetEnvironmentRevision() != TerrainRevision)
	{
		// The planet's lighting was rebuilt from its base row; put the weather back on top of it.
		TerrainRevision = Terrain->GetEnvironmentRevision();
		TargetRow = nullptr;
	}

	bIndoors = CurrentCell.IsValid();
	const double Now = FPlatformTime::Seconds();
	if (!bIndoors && (Family.IsEmpty() || Now - LastFamilySample >= FamilySampleSeconds))
	{
		LastFamilySample = Now;
		const APawn* Pawn = Controller->GetPawn();
		const FString NewFamily = Terrain->GetEnvironmentFamilyAt(Pawn ? Pawn->GetActorLocation() : Controller->GetFocalLocation());
		if (!NewFamily.IsEmpty())
		{
			Family = NewFamily;
		}
	}

	const FSWGEnvironmentRow* Wanted = USWGAmbienceSubsystem::FindEnvironmentRow(Rows, Family, WeatherIndex);
	if (Wanted && Wanted != TargetRow)
	{
		StartBlend(*Wanted, /*bSnap=*/TargetRow == nullptr);
	}

	if (TargetRow && BlendAlpha < 1.f)
	{
		const float BlendSeconds = CVarBlendSeconds.GetValueOnGameThread();
		BlendAlpha = BlendSeconds > 0.f ? FMath::Min(1.f, BlendAlpha + DeltaSeconds / BlendSeconds) : 1.f;
		Terrain->BlendEnvironment(&FromRow, ToRow, BlendAlpha);
	}
	ApplyWind(BlendAlpha);

	const APlayerCameraManager* Camera = Controller->PlayerCameraManager;
	UpdateParticle(World, Camera ? Camera->GetCameraLocation() : Controller->GetFocalLocation(), Camera ? Camera->GetCameraRotation().Yaw : 0.f);
	return true;
}

void USWGWeatherSubsystem::StartBlend(const FSWGEnvironmentRow& NewRow, bool bSnap)
{
	if (bSnap)
	{
		FromRow = NewRow;
		BlendAlpha = 1.f;
	}
	else
	{
		// A blend cut short restarts from whichever end it was nearer.
		FromRow = BlendAlpha >= 0.5f ? ToRow : FromRow;
		BlendAlpha = 0.f;
	}
	TargetRow = &NewRow;
	ToRow = NewRow;
	Terrain->BlendEnvironment(&FromRow, ToRow, BlendAlpha);
	UE_LOG(LogTemp, Log, TEXT("USWGWeatherSubsystem: %s -> family %s weather %d, particle '%s'%s"),
		*PlanetName, *NewRow.Family, NewRow.WeatherIndex, *NewRow.CameraParticle, bSnap ? TEXT(" (snap)") : TEXT(""));
}

void USWGWeatherSubsystem::ApplyWind(float Alpha)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || !bHasInput || !TargetRow)
	{
		return;
	}

	const float Scale = FMath::Lerp(FromRow.WindSpeedScale, ToRow.WindSpeedScale, Alpha);
	const FVector WindSpeed = ComputeWindSpeed(WindVector, Scale);
	if (USWGEffectsSubsystem* Effects = World->GetSubsystem<USWGEffectsSubsystem>())
	{
		Effects->SetWind(WindSpeed);
	}
	if (USWGPropAttachmentsSubsystem* Props = World->GetSubsystem<USWGPropAttachmentsSubsystem>())
	{
		Props->SetWindStrength(ComputeWindStrength(WindSpeed));
	}
}

void USWGWeatherSubsystem::UpdateParticle(UWorld* World, const FVector& CameraLocation, float CameraYaw)
{
	USWGEffectsSubsystem* Effects = World->GetSubsystem<USWGEffectsSubsystem>();
	const bool bWanted = Effects && TargetRow && !bIndoors && CVarParticles.GetValueOnGameThread() != 0 && !TargetRow->CameraParticle.IsEmpty();
	if (!bWanted)
	{
		if (!PlayedParticle.IsEmpty())
		{
			StopParticle();
		}
		return;
	}

	if (!IsValid(ParticleAnchor))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		ParticleAnchor = World->SpawnActor<AActor>(CameraLocation, FRotator::ZeroRotator, SpawnParams);
		if (!ParticleAnchor)
		{
			return;
		}
		USceneComponent* Root = NewObject<USceneComponent>(ParticleAnchor, TEXT("SWGWeatherRoot"));
		ParticleAnchor->SetRootComponent(Root);
		Root->RegisterComponent();
#if WITH_EDITOR
		ParticleAnchor->SetActorLabel(TEXT("SWGWeather"));
#endif
	}
	// The emitter follows the camera's position and yaw but not its pitch or roll, so rain keeps falling
	// straight down in the view when looking up or down.
	ParticleAnchor->SetActorLocationAndRotation(CameraLocation, FRotator(0.f, CameraYaw, 0.f));

	const double Now = FPlatformTime::Seconds();
	const bool bChanged = TargetRow->CameraParticle != PlayedParticle;
	const bool bEnded = !bChanged && ParticleHandle.IsValid() && Now - LastParticleCheck >= ParticleCheckSeconds && !ParticleHandle.IsActive();
	if (bChanged || bEnded)
	{
		LastParticleCheck = Now;
		// Stopping only ends emission: the old particles live out their lives while the new ones build up.
		ParticleHandle.Stop(false);
		ParticleHandle = Effects->PlayParticleAttached(TargetRow->CameraParticle, ParticleAnchor->GetRootComponent());
		PlayedParticle = TargetRow->CameraParticle;
	}
}

void USWGWeatherSubsystem::StopParticle()
{
	ParticleHandle.Stop(false);
	ParticleHandle = FSWGEffectHandle();
	PlayedParticle.Reset();
}

TArray<FString> USWGWeatherSubsystem::DescribeState() const
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("weather %d%s, wind (%.2f, %.2f, %.2f) from %s"), WeatherIndex, bIndoors ? TEXT(" (indoors: particle off)") : TEXT(""),
		WindVector.X, WindVector.Y, WindVector.Z, bHasInput ? TEXT("server/console") : TEXT("nothing yet")));
	Lines.Add(FString::Printf(TEXT("planet %s, player family %s, %d environment row(s)"), *PlanetName, *Family, Rows.Num()));
	if (!TargetRow)
	{
		Lines.Add(TEXT("no environment row applied"));
		return Lines;
	}

	Lines.Add(FString::Printf(TEXT("row %s / weather %d, blend %.0f%%"), *TargetRow->Family, TargetRow->WeatherIndex, BlendAlpha * 100.f));
	Lines.Add(FString::Printf(TEXT("camera particle '%s' %s"), *TargetRow->CameraParticle, ParticleHandle.IsActive() ? TEXT("PLAYING") : TEXT("not playing")));
	Lines.Add(FString::Printf(TEXT("fog %s %.5f..%.5f -> density %.5f, ramp '%s'"), TargetRow->bFogEnabled ? TEXT("on") : TEXT("off"),
		TargetRow->MinFogDensity, TargetRow->MaxFogDensity, TargetRow->GetFogDensity(), *TargetRow->ColorRamp));
	const FVector WindSpeed = ComputeWindSpeed(WindVector, TargetRow->WindSpeedScale);
	Lines.Add(FString::Printf(TEXT("wind scale %.1f -> %.1f m/s (%.1f, %.1f), flag strength %.2f"), TargetRow->WindSpeedScale,
		FVector2D(WindSpeed.X, WindSpeed.Z).Size(), WindSpeed.X, WindSpeed.Z, ComputeWindStrength(WindSpeed)));
	return Lines;
}
