#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Playback/SWGSoundHandle.h"
#include "Subsystems/SWGEnvironmentRow.h"
#include "SWGAmbienceSubsystem.generated.h"

class ASWGCell;
class ASWGSoundEmitter;
class UReverbEffect;
class USWGAudioSubsystem;
class USWGMeshGeneratorSubsystem;
class USWGTerrainSubsystem;
class USWGTreSubsystem;
class USWGWeatherSubsystem;
struct FSWGInteriorRow;

/**
 * World and building ambience for the local player.
 *
 * Outdoors, the .trn environment family under the player (EGRP/AENV) picks a row of the planet's
 * environment table, whose day and night ambient loops (two slots) crossfade as the sun rises and sets.
 * Indoors, the room's interior.iff row replaces them and its Room Type picks a reverb preset.
 * Music is pushed onto the music manager's Environment layer as one-shots (every theme here has a loop
 * count of 1): the family's First Music on zone-in and family change, Sunrise/Sunset at the transition.
 * Also keeps the sound emitters (sound objects) playing only while the listener is near.
 */
UCLASS()
class SWGEMUCLIENT_API USWGAmbienceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Rows of datatables/environment/<planet>.iff; false when the planet has no table. */
	static bool LoadEnvironmentTable(const USWGTreSubsystem& Tre, const FString& PlanetName, TArray<FSWGEnvironmentRow>& OutRows);

	/** Environment table loader shared by ambience, terrain and weather. The family's row for a weather index, else its next milder one, else "global", else the table's first clear-weather row. */
	static const FSWGEnvironmentRow* FindEnvironmentRow(const TArray<FSWGEnvironmentRow>& Rows, const FString& FamilyName, int32 WeatherIndex);

	// -- Sound emitters ---------------------------------------------------

	void RegisterEmitter(ASWGSoundEmitter* Emitter);
	void UnregisterEmitter(ASWGSoundEmitter* Emitter);
	const TArray<TWeakObjectPtr<ASWGSoundEmitter>>& GetEmitters() const { return Emitters; }

	/** The existing ASND loops of a sound object template (cached). */
	const TArray<FString>& ResolveSoundObjectLoops(const FString& TemplatePath);

	// -- Diagnostics ------------------------------------------------------

	/** Multi-line state for swg.Audio.Ambience. */
	TArray<FString> DescribeState() const;

private:
	struct FSlot
	{
		FString Path;
		FSWGSoundHandle Handle;
	};

	bool Tick(float DeltaSeconds);
	void UpdateEmitters(const FVector& ListenerLocation);
	void ResetPlanet(const FString& PlanetName);
	void UpdateRoom(UWorld* World);
	void UpdateOutdoorFamily(const FVector& PlayerLocation);
	void UpdateAmbientSlots(bool bDay);
	void SetSlotPath(int32 SlotIndex, const FString& Path);
	void StopSlots(float FadeSeconds);
	void PlayMusicOnce(const FString& Key, const FString& SndPath);
	void SetReverb(UWorld* World, int32 RoomType);
	void ClearReverb(UWorld* World);
	void HandleRoomChanged(ASWGCell* Cell);

	UPROPERTY()
	TObjectPtr<USWGAudioSubsystem> Audio;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	UPROPERTY()
	TObjectPtr<USWGTerrainSubsystem> Terrain;

	UPROPERTY()
	TObjectPtr<USWGMeshGeneratorSubsystem> MeshGenerator;

	UPROPERTY()
	TObjectPtr<USWGWeatherSubsystem> Weather;

	UPROPERTY()
	TMap<int32, TObjectPtr<UReverbEffect>> ReverbEffects;

	TArray<TWeakObjectPtr<ASWGSoundEmitter>> Emitters;
	TMap<FString, TArray<FString>> SoundObjectLoops;

	FString PlanetName;
	TArray<FSWGEnvironmentRow> EnvironmentRows;
	FString Family;
	const FSWGEnvironmentRow* EnvironmentRow = nullptr;

	/** Set by the room delegate, applied on the next tick so a leave-then-enter in one frame is one change. */
	bool bRoomDirty = false;
	TWeakObjectPtr<ASWGCell> PendingCell;
	TWeakObjectPtr<ASWGCell> CurrentCell;
	const FSWGInteriorRow* InteriorRow = nullptr;
	FString InteriorMatchName;
	int32 ReverbRoomType = INDEX_NONE;

	FSlot Slots[2];
	bool bDay = true;
	bool bDayKnown = false;

	/** Last time each music key (family or interior row) played its First Music. */
	TMap<FString, double> MusicLastPlayed;

	double LastOutdoorSample = 0.0;
	double LastEmitterUpdate = 0.0;
	int32 ActiveEmitterLoops = 0;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle RoomChangedHandle;
};
