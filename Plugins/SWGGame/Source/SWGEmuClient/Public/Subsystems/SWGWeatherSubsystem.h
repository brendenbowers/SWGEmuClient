#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGEffectsTypes.h"
#include "Subsystems/SWGEnvironmentRow.h"
#include "SWGWeatherSubsystem.generated.h"

class AActor;
class ASWGCell;
class USWGNetworkSubsystem;
class USWGTerrainSubsystem;
class USWGTreSubsystem;
struct FSWGNetMessage;

/**
 * The local player's weather. The server sends a weather index (0 clear .. 4 extreme storm) and a wind
 * vector (ServerWeatherMessage); swg.Weather sets the same for offline testing. Each index selects a row of
 * the planet's environment table for the family under the player, and a change blends over
 * swg.Weather.BlendSeconds:
 *  - the row's camera .prt (rain sheets, dust storm, fog, leaves) is stopped and the new one started, so the
 *    old particles die out while the new ones build up; suppressed while the player is in a building room;
 *  - fog density, the lighting colour ramp, the gradient sky and the cloud layers go through
 *    USWGTerrainSubsystem::BlendEnvironment;
 *  - wind (vector x row Wind Speed Scale) drives particle wind resistance and the flags' 0..1 strength.
 */
UCLASS()
class SWGEMUCLIENT_API USWGWeatherSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 0 (clear) .. 4 (extreme storm). */
	int32 GetWeatherIndex() const { return WeatherIndex; }

	/** The server's wind in SWG axes (x east, y up, z north), each component in [-1, 1]. */
	const FVector& GetWindVector() const { return WindVector; }

	/** From ServerWeatherMessage or swg.Weather. Index is clamped; the change blends in over the next seconds. */
	void SetWeather(int32 NewWeatherIndex, const FVector& NewWindVector);

	/** Wind in m/s, sim space (the effects' x right, y up, z forward): the horizontal wire vector times the row's Wind Speed Scale. */
	static FVector ComputeWindSpeed(const FVector& WindVector, float WindSpeedScale);

	/** Flag strength 0..1 for a wind speed in m/s: horizontal speed over the strongest Wind Speed Scale in the tables (15). */
	static float ComputeWindStrength(const FVector& WindSpeed);

	/** Multi-line state for swg.Weather.Info. */
	TArray<FString> DescribeState() const;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FSWGSpaceSceneTest;
#endif
	bool Tick(float DeltaSeconds);
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message);
	void HandleRoomChanged(ASWGCell* Cell);
	void ResetPlanet(const FString& NewPlanetName);
	void StartBlend(const FSWGEnvironmentRow& NewRow, bool bSnap);
	void ApplyWind(float Alpha);
	void UpdateParticle(UWorld* World, const FVector& CameraLocation, float CameraYaw);
	void StopParticle();

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	UPROPERTY()
	TObjectPtr<USWGTerrainSubsystem> Terrain;

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	/** Carries the camera particle; moved to the camera every frame. */
	UPROPERTY()
	TObjectPtr<AActor> ParticleAnchor;

	int32 WeatherIndex = 0;
	FVector WindVector = FVector::ZeroVector;
	/** False until the server or swg.Weather has spoken, so the weather does not override swg.Fx.Wind before then. */
	bool bHasInput = false;

	FString PlanetName;
	TArray<FSWGEnvironmentRow> Rows;
	FString Family;

	/** The row the weather is moving to (into Rows), with copies of the blend's two ends for the terrain. */
	const FSWGEnvironmentRow* TargetRow = nullptr;
	FSWGEnvironmentRow FromRow;
	FSWGEnvironmentRow ToRow;
	float BlendAlpha = 1.f;
	int32 TerrainRevision = -1;

	TWeakObjectPtr<ASWGCell> CurrentCell;
	bool bIndoors = false;

	FSWGEffectHandle ParticleHandle;
	FString PlayedParticle;
	double LastParticleCheck = 0.0;
	double LastFamilySample = 0.0;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle MessageHandle;
	FDelegateHandle RoomChangedHandle;
};
