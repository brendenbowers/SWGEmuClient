#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SWGMissionSubsystem.generated.h"

class USWGNetworkSubsystem;
class USWGObjectGraphSubsystem;
class USWGTreSubsystem;
class USWGRadialMenuSubsystem;
struct FSWGNetMessage;

/** One row a mission browser window would show. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGMissionEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Mission")
	int64 ObjectId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Mission")
	FText Title;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Mission")
	FText Description;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Mission")
	FString TargetName;

	/**
	 * The mission's target — usually the lair/camp/NPC spawn the mission
	 * points at (a destroy/deliver mission's nest, a bounty's quarry).
	 * Resolved from TargetTemplateCrc's own objectName field
	 * (USWGTreSubsystem::ResolveTemplateObjectName), not from TargetName
	 * (a raw, unresolved "@table:key" reference) — this is what actually
	 * reads as a real name ("Bull Bantha Lair").
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Mission")
	FText TargetTemplateName;

	/** The CRC TargetTemplateName was resolved from — what a details pane's 3D preview asks USWGItemIconSubsystem::RequestModelForTemplateCrc for. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Mission")
	int32 TargetTemplateCrc = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Mission")
	int32 RewardCredits = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Mission")
	int32 DifficultyDisplay = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Mission")
	int32 TypeCRC = 0;

	/** True once a MissionObjectDeltaMessage3 has actually filled this row in — a MISO's baseline alone is blank. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Mission")
	bool bPopulated = false;

	/** Raw (SWG-space) start position, from delta 0x09 — zero until that update arrives. */
	UPROPERTY()
	FVector StartPositionRaw = FVector::ZeroVector;

	UPROPERTY()
	bool bHasStartPosition = false;

	/** Distance from the player to StartPositionRaw, in meters — filled in by GetMissions(), not tracked per-update. Assumes the mission is on the player's current planet; there's no client-side planet check yet. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Mission")
	float DistanceMeters = 0.f;

	/** Compass bearing from the player to the mission ("N", "NE", ...), or empty if unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Mission")
	FString Direction;

	/** 0-360, north-relative clockwise bearing behind Direction — what sorting by Direction actually orders on. */
	UPROPERTY()
	float BearingDegrees = 0.f;

	/**
	 * Echoed from the MissionListRequest seq that last (re)populated this
	 * slot — MissionManager::populateMissionList stamps it via
	 * setRefreshCounter. Every mission terminal shares one mission_bag, and
	 * a terminal only randomizes its own slot range, so this is what tells
	 * "the general terminal's missions" apart from "whatever some other
	 * terminal wrote into other slots earlier": GetMissions() only returns
	 * entries whose RefreshCounter matches the active terminal's most recent
	 * request, not every populated slot in the whole bag.
	 */
	UPROPERTY()
	uint32 RefreshCounter = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSWGOnMissionWindowRequested, int64, TerminalObjectId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnMissionListChanged);

/**
 * Tracks the local player's mission_bag: which MissionObjects it holds and
 * what MissionManager::populateMissionList has filled into them, so a
 * mission browser widget has something to read.
 *
 * Mission terminal "Use" doesn't go through the generic SUI window system —
 * see FMissionListRequest — so this exists specifically to decode MISO
 * baselines/deltas (Network/Objects/Zone/Mission/MissionObjectBaseline.h)
 * and expose them as a simple list, independent of whether MISO objects ever
 * get a spawned actor (they don't need one; USWGObjectGraphSubsystem tracks
 * containment for any object id regardless).
 */
UCLASS()
class SWGEMUCLIENT_API USWGMissionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Fired the moment "Use" is picked on a mission terminal — open the browser now, list fills in as data arrives. */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Mission")
	FSWGOnMissionWindowRequested OnMissionWindowRequested;

	/** Fired whenever a tracked mission's data changes, or the mission_bag's contents do. */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Mission")
	FSWGOnMissionListChanged OnMissionListChanged;

	/** Every mission currently in the player's mission_bag, populated or not. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Mission")
	TArray<FSWGMissionEntry> GetMissions() const;

	/** The terminal id from the most recent OnMissionWindowRequested, or 0. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Mission")
	int64 GetActiveTerminalId() const { return ActiveTerminalId; }

	/** Re-sends FMissionListRequest for the active terminal — what a mission browser's Refresh button calls. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Mission")
	bool RefreshMissionList();

	/** Sends FMissionAccept for MissionObjectId against the active terminal. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Mission")
	bool AcceptMission(int64 MissionObjectId);

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message);

	UFUNCTION()
	void HandleMissionTerminalUsed(int64 TerminalObjectId);

	/** Finds the player's mission_bag by template path, like USWGItemTransferSubsystem::FindInventoryBagId does for the inventory. */
	int64 FindMissionBagId() const;

	/** Sends FMissionListRequest for TerminalObjectId with a fresh seq, remembered as LastRequestSeq for GetMissions()'s filter. */
	bool SendMissionListRequest(int64 TerminalObjectId);

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	UPROPERTY()
	TObjectPtr<USWGRadialMenuSubsystem> RadialMenu;

	FDelegateHandle MessageHandle;

	int64 ActiveTerminalId = 0;

	/** Seq of our own last MissionListRequest — GetMissions() only shows entries whose RefreshCounter matches this. */
	uint32 NextRequestSeq = 0;
	uint32 LastRequestSeq = 0;

	/** Cached once resolved; mission_bag doesn't move once the character's loaded. */
	mutable int64 MissionBagId = 0;

	TMap<int64, FSWGMissionEntry> Entries;
};
