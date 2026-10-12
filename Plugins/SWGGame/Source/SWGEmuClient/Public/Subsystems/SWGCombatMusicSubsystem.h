#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TRE/SWGGameMusicReader.h"
#include "SWGCombatMusicSubsystem.generated.h"

class USWGAudioSubsystem;
class USWGCombatSubsystem;
class USWGObjectGraphSubsystem;
class USWGTreSubsystem;
struct FCombatActionIn;
struct FSWGResolvedCombatDefender;

/**
 * Drives the music manager's Combat layer from the local player's own state, using the
 * scene/game_music_manager.iff entries for whoever the player is fighting:
 *  - the Combat state bit rising starts the fight loop (ENTR);
 *  - falling again plays the victory sting (EXIT) if the opponent went down, else just fades out;
 *  - being incapacitated or killed plays the INCP / DETH sting.
 * Stings end by themselves, which hands the channel back to the lower layers.
 */
UCLASS()
class SWGEMUCLIENT_API USWGCombatMusicSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return ObjectGraph != nullptr; }

	/**
	 * The role a template stands for. The file names three (creature, NPC, player) but no data says
	 * which mobile is which, so this is inferred from the template path: object/creature/player/ is a
	 * player, dressed_* mobiles are NPCs, everything else is a creature.
	 */
	static ESWGCombatMusicRole ClassifyTemplate(const FString& TemplatePath);

private:
	void HandleCombatActionResolved(const FCombatActionIn& Action, const TArray<FSWGResolvedCombatDefender>& Defenders);

	const FSWGGameMusicData* GetMusicData();
	ESWGCombatMusicRole ClassifyObject(int64 ObjectId) const;

	/** Sets the Combat layer to a role's entry from Entries, or clears it for an empty path. */
	void SetCombatLayer(const TArray<FString>& Entries, ESWGCombatMusicRole Role);
	void ClearCombatLayer();

	void StartCombat(int64 PlayerId);
	void EndCombat();

	UPROPERTY()
	TObjectPtr<USWGCombatSubsystem> Combat;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	UPROPERTY()
	TObjectPtr<USWGAudioSubsystem> Audio;

	TSharedPtr<FSWGGameMusicData> MusicData;
	bool bMusicLoadAttempted = false;

	bool bInCombat = false;
	/** 0 while up; otherwise the downed posture (incapacitated or dead) the sting was last played for. */
	uint8 DownedPosture = 0;
	ESWGCombatMusicRole Role = ESWGCombatMusicRole::Creature;
	int64 OpponentId = 0;

	/** Latest object the local player swung at, or that swung at them: the fallback when no target is selected. */
	int64 LastOpponentId = 0;

	FDelegateHandle ResolvedHandle;
};
