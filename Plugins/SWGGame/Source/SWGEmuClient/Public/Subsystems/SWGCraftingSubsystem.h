#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Network/Objects/Zone/Object/CraftingDraftSlot.h"
#include "Network/Objects/Zone/ManufactureSchematic/ManufactureSchematicBaseline.h"
#include "Network/Messages/Zone/Object/CraftingMessages.h"
#include "SWGCraftingSubsystem.generated.h"

class USWGCommandSubsystem;
class USWGCraftingComponent;
class USWGNetworkSubsystem;
class USWGObjectGraphSubsystem;
class USWGRadialMenuSubsystem;
class USWGTreSubsystem;
struct FSWGNetMessage;
struct FSWGPacket;

/**
 * Client-tracked mirror of CraftingSessionImplementation::state
 * (server/zone/objects/player/sessions/crafting/CraftingSessionImplementation.cpp).
 * Driven by PLAY base9's CraftingState field (USWGCraftingComponent), which is
 * the authority — the server alone decides how nextCraftingStage collapses
 * states (e.g. 4 -> 6 in one call when nothing needs experimenting), so this
 * is read off the component rather than inferred from which messages arrive.
 */
UENUM(BlueprintType)
enum class ESWGCraftingSessionState : uint8
{
	None              = 0, // no session, or one just closed
	ChoosingSchematic = 1, // the 0x102 schematic list is up
	Assembling        = 2, // a schematic was picked; filling slots, no experimenting yet
	Experimenting     = 3, // assembled once; a station is present and has experiment rows left
	Assembled         = 4, // assembled; nothing left to experiment (or no station)
	Customizing       = 5, // customization (0x15A) was applied
	ReadyToFinish     = 6, // createPrototype/createManfSchematic is valid
};

/** One schematic the current session's tool/station offers (from 0x102). */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGCraftingSchematicOption
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	int32 SchematicCrc = 0;

	/** Bitmask matching one of the tool's enabledTabs values — see crafting-protocol.md; captions are ours to author, not data-driven. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	int32 ToolTab = 0;

	/** Best-effort display name: the template filename with underscores turned to spaces. Stage 3 can resolve the real STF name from the .iff itself. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	FString Name;

	/** object/draft_schematic/... path, if TRE has resolved this CRC yet. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	FString TemplatePath;
};

/** One required ingredient slot, merged from the 0x103/0x1BF requirement with whatever the MSCO base7 says is filled. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGCraftingSlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	bool bOptional = false;

	/** Resource class ENUM this slot accepts; empty for a component/identical slot. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	FString ResourceType;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	ESWGDraftSlotKind Kind = ESWGDraftSlotKind::None;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	int32 RequiredQuantity = 0;

	/** Sum of what MSCO base7's slotQuantities reports for this slot. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	int32 FilledQuantity = 0;

	/** Object ids currently placed here (MSCO base7 slotOIDs) — see the inference caveat on FManufactureSchematicBaseline::SlotOIDs. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	TArray<int64> FilledObjectIds;

	/** Average resource quality placed here, 0-1. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	float Quality = 0.f;

	bool IsFull() const { return FilledQuantity >= RequiredQuantity && RequiredQuantity > 0; }
};

/** One experimentation row, as the assembly/experiment page shows it. */
USTRUCT(BlueprintType)
struct SWGEMUCLIENT_API FSWGCraftingExperimentGroup
{
	GENERATED_BODY()

	/** Internal attribute name (crafting.stf exp_* key) — Stage 3 resolves the caption. */
	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	FString Title;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	float CurrentPercent = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "SWGEmu|Crafting")
	float MaxPercent = 1.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnCraftingDraftPreviewChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnCraftingSessionStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnCraftingStageChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnCraftingSlotsChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSWGOnCraftingSlotResult, ESWGCraftingSlotResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSWGOnCraftingAssemblyResult, ESWGCraftingResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSWGOnCraftingExperimentResult, ESWGCraftingResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnCraftingSessionClosed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSWGOnCraftingToolStatusChanged);

/**
 * Owns the crafting session protocol: a "Use" on a crafting tool/station
 * (wired in USWGRadialMenuSubsystem's ITEM_USE rule) sends
 * requestCraftingSession, and everything from there — the schematic list,
 * picking one, filling slots, assembling, experimenting, customizing and
 * finishing — is driven through this subsystem. See
 * Plugins/SWGEmu/crafting-protocol.md for the wire format this decodes.
 *
 * State comes from two places: ESWGCraftingSessionState mirrors PLAY base9's
 * CraftingState (read off the local player's USWGCraftingComponent, the
 * authority — see the enum's own comment for why); everything else
 * (schematics, slots, experiment rows, tool/station/MSCO ids) comes from the
 * crafting-specific ObjController messages and the session's own MSCO
 * baselines/deltas, listened to directly off USWGNetworkSubsystem — the same
 * "subsystem parses its own raw messages" shape as USWGSurveySubsystem and
 * USWGIntangibleObjectSubsystem, not the actor-component handler registry
 * (BaselineHandlers/DeltaHandlers) other object types use, because an MSCO
 * has nothing to render and doesn't need a spawned actor.
 */
UCLASS()
class SWGEMUCLIENT_API USWGCraftingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** A requestDraftSlotsBatch/requestResourceWeightsBatch reply arrived — see GetDraftSlotsPreview/GetResourceWeightsPreview. */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Crafting")
	FSWGOnCraftingDraftPreviewChanged OnDraftPreviewChanged;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Crafting")
	FSWGOnCraftingSessionStarted OnSessionStarted;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Crafting")
	FSWGOnCraftingStageChanged OnStageChanged;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Crafting")
	FSWGOnCraftingSlotsChanged OnSlotsChanged;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Crafting")
	FSWGOnCraftingSlotResult OnSlotResult;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Crafting")
	FSWGOnCraftingAssemblyResult OnAssemblyResult;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Crafting")
	FSWGOnCraftingExperimentResult OnExperimentResult;

	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Crafting")
	FSWGOnCraftingSessionClosed OnSessionClosed;

	/**
	 * Declared per the crafting plan; nothing fires it yet. The tool's
	 * working/finished countdown delivery mechanism (a pushed message vs. an
	 * attribute-list a UI would have to poll) is still untraced — see
	 * crafting-protocol.md's Stage 2 carry-forward list.
	 */
	UPROPERTY(BlueprintAssignable, Category = "SWGEmu|Crafting")
	FSWGOnCraftingToolStatusChanged OnToolStatusChanged;

	// ── State ────────────────────────────────────────────────────────────

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	ESWGCraftingSessionState GetState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	bool IsSessionOpen() const { return CurrentState != ESWGCraftingSessionState::None; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	int64 GetToolObjectId() const { return ToolObjectId; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	int64 GetStationObjectId() const { return StationObjectId; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	int64 GetManufactureSchematicId() const { return ManufactureSchematicId; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	int64 GetPrototypeId() const { return PrototypeId; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	const TArray<FSWGCraftingSchematicOption>& GetSchematics() const { return Schematics; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	int32 GetSelectedSchematicIndex() const { return SelectedSchematicIndex; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	bool GetAllowFactoryRun() const { return bAllowFactory; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	const TArray<FSWGCraftingSlot>& GetSlots() const { return Slots; }

	/** True once every non-optional slot's IsFull() — what gates the Assemble button. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	bool IsAssemblyReady() const
	{
		for (const FSWGCraftingSlot& Slot : Slots)
		{
			if (!Slot.bOptional && !Slot.IsFull())
			{
				return false;
			}
		}
		return !Slots.IsEmpty();
	}

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	const TArray<FSWGCraftingExperimentGroup>& GetExperimentGroups() const { return ExperimentGroups; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	int32 GetExperimentPointsTotal() const { return ExperimentPointsTotal; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	int32 GetExperimentPointsRemaining() const { return ExperimentPointsRemaining; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	float GetFailureRate() const { return FailureRate; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	const TArray<FString>& GetTemplateChoices() const { return TemplateChoices; }

	/** Customization variable names (MSCO7 update 0x0D), index-parallel with GetCustomizationVarDefaults/GetCustomizationPaletteCounts. */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	const TArray<FString>& GetCustomizationVarNames() const { return CustomizationVarNames; }

	/** Each variable's starting value (MSCO7 update 0x0E). */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	const TArray<int32>& GetCustomizationVarDefaults() const { return CustomizationVarDefaults; }

	/** Each variable's palette colour count, 0 for a non-palette (numeric range) variable (MSCO7 update 0x10). */
	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	const TArray<int32>& GetCustomizationPaletteCounts() const { return CustomizationPaletteCounts; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	FString GetPrototypeName() const { return CurrentSchematic.CustomName.IsEmpty() ? CurrentSchematic.ObjectNameName : CurrentSchematic.CustomName; }

	UFUNCTION(BlueprintPure, Category = "SWGEmu|Crafting")
	int32 GetManufactureLimit() const { return CurrentSchematic.ManufactureLimit; }

	/** The raw decoded MSCO state (base3/6/7), for anything the flattened getters above don't surface. */
	const FManufactureSchematicBaseline& GetSchematicBaseline() const { return CurrentSchematic; }

	// ── Actions ──────────────────────────────────────────────────────────
	// Every Request* returns false only when there's nothing to send through
	// (no session, wrong state, no Commands/Network); the server's own reply
	// (a 0x10C/0x1BE/0x113, or a PLAY9 state change) is what actually moves
	// the session forward — these don't optimistically update local state.

	/** Sends requestCraftingSession at Tool (or a station); normally reached via the tool's "Use" radial, not called directly. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Crafting")
	bool StartSession(int64 ToolOrStationObjectId);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Crafting")
	bool SelectSchematic(int32 Index);

	/** Asks the MSCO for its current slot state; call once after SelectSchematic, or to re-sync. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Crafting")
	bool ListenSlots();

	/**
	 * Requests the Draft page's browse-only preview (required slots + resource
	 * weights) for schematics not yet in the cache — no session/selection
	 * needed (`requestDraftSlotsBatch`/`requestResourceWeightsBatch`).
	 * Results arrive via OnDraftPreviewChanged; already-cached CRCs are
	 * skipped, so it's safe to call every time the tab selection changes.
	 */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Crafting")
	bool RequestDraftPreview(const TArray<int32>& SchematicCrcs);

	/** Cached required slots for SchematicCrc, or nullptr if RequestDraftPreview hasn't returned it yet. */
	const FSWGCraftingDraftSlotsIn* GetDraftSlotsPreview(int32 SchematicCrc) const { return DraftSlotsPreview.Find(SchematicCrc); }

	/** Cached resource weights for SchematicCrc, or nullptr if RequestDraftPreview hasn't returned it yet. */
	const FSWGCraftingResourceWeightsIn* GetResourceWeightsPreview(int32 SchematicCrc) const { return ResourceWeightsPreview.Find(SchematicCrc); }

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Crafting")
	bool AddIngredient(int64 IngredientObjectId, int32 Slot);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Crafting")
	bool RemoveIngredient(int32 Slot, int64 IngredientObjectId);

	/** Assembles (state 2/3) or advances past assembly (state 4/5/6) — same server command either way. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Crafting")
	bool Assemble();

	/** Only valid at ESWGCraftingSessionState::Experimenting. */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Crafting")
	bool Experiment(const TArray<FSWGCraftingExperimentRow>& Rows);

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Crafting")
	bool Customize(const FString& Name, uint8 TemplateChoice, int32 SchematicCount, const TArray<FSWGCraftingCustomizationEdit>& Edits);

	/** Only valid at ESWGCraftingSessionState::ReadyToFinish. bPractice skips the item (no hopper delay, no item — just XP). */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Crafting")
	bool CreatePrototype(bool bPractice);

	/** Only valid at ESWGCraftingSessionState::ReadyToFinish, and only when GetAllowFactoryRun(). */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Crafting")
	bool CreateManufactureSchematic();

	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Crafting")
	bool Cancel();

	/** Picks the tool's "Retrieve Output" radial option (server-drawn, only offered once the tool has finished items in its hopper). */
	UFUNCTION(BlueprintCallable, Category = "SWGEmu|Crafting")
	bool RetrieveOutput(int64 ToolObjId);

	/** Feeds a fake schematic list, as if 0x102 had arrived, for UI work without a server (swg.Craft.Fake). */
	void InjectSchematics(const TArray<FSWGCraftingSchematicOption>& InSchematics);
	/** Feeds fake slots and experiment data, as if the session had progressed that far. */
	void InjectSlotsAndExperiment(const TArray<FSWGCraftingSlot>& InSlots, const TArray<FSWGCraftingExperimentGroup>& InGroups, int32 PointsTotal, int32 PointsRemaining);
	/** Feeds fake slots at the Assembling stage (no experiment rows yet), for holo Assembly UI work without a real tool. */
	void InjectAssemblySlots(const TArray<FSWGCraftingSlot>& InSlots);

private:
	void HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message);
	void HandleObjControllerMessage(const struct FObjControllerMessageIn& Envelope);
	void HandleSchematicList(FSWGPacket& Payload);
	void HandleIngredientSlots(FSWGPacket& Payload);
	void HandleSlotReply(FSWGPacket& Payload);
	void HandleAssemblyResult(FSWGPacket& Payload);
	void HandleExperimentResult(FSWGPacket& Payload);
	void HandleCloseWindow(FSWGPacket& Payload);
	void HandleDraftSlots(FSWGPacket& Payload);
	void HandleResourceWeights(FSWGPacket& Payload);
	void HandleBaselinesMessage(const struct FBaselinesMessage& Baselines);
	void HandleDeltasMessage(const struct FDeltasMessage& Deltas);

	/** Finds (and, if it changed, rebinds to) the local player's USWGCraftingComponent — pawns respawn across zones. */
	void EnsureCraftingComponentBound();
	UFUNCTION() void HandleCraftingComponentChanged();

	/** Rebuilds Slots from Schematics[SelectedSchematicIndex]'s requirements (SlotRequirements) merged with CurrentSchematic's filled state (base7). */
	void RebuildSlots();

	void ClearSession();
	uint8 NextCounter();

	UPROPERTY()
	TObjectPtr<USWGNetworkSubsystem> Network;

	UPROPERTY()
	TObjectPtr<USWGCommandSubsystem> Commands;

	UPROPERTY()
	TObjectPtr<USWGObjectGraphSubsystem> ObjectGraph;

	UPROPERTY()
	TObjectPtr<USWGTreSubsystem> Tre;

	UPROPERTY()
	TObjectPtr<USWGRadialMenuSubsystem> Radial;

	/** The component OnCraftingStateChanged is currently bound to; rebound when the local pawn (re)spawns. */
	TWeakObjectPtr<USWGCraftingComponent> BoundCraftingComponent;

	FDelegateHandle MessageHandle;

	ESWGCraftingSessionState CurrentState = ESWGCraftingSessionState::None;

	int64 ToolObjectId = 0;
	int64 StationObjectId = 0;
	int64 ManufactureSchematicId = 0;
	int64 PrototypeId = 0;
	bool bAllowFactory = false;

	TArray<FSWGCraftingSchematicOption> Schematics;
	int32 SelectedSchematicIndex = INDEX_NONE;

	/** The 0x103/0x1BF requirement list for the selected schematic — Slots is rebuilt from this plus CurrentSchematic's base7 fill state. */
	TArray<FSWGDraftSlot> SlotRequirements;
	TArray<FSWGCraftingSlot> Slots;

	TArray<FSWGCraftingExperimentGroup> ExperimentGroups;
	int32 ExperimentPointsTotal = 0;
	int32 ExperimentPointsRemaining = 0;
	float FailureRate = 0.f;
	TArray<FString> TemplateChoices;
	TArray<FString> CustomizationVarNames;
	TArray<int32> CustomizationVarDefaults;
	TArray<int32> CustomizationPaletteCounts;

	/** Decoded MSCO base3/6/7 for the schematic under construction. */
	FManufactureSchematicBaseline CurrentSchematic;

	uint8 ClientCounter = 1;

	/** Browse-only preview cache for the Draft page, keyed by SchematicCrc — see RequestDraftPreview. */
	TMap<int32, FSWGCraftingDraftSlotsIn> DraftSlotsPreview;
	TMap<int32, FSWGCraftingResourceWeightsIn> ResourceWeightsPreview;
};
