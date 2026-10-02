#include "Subsystems/SWGCraftingSubsystem.h"
#include "SWGLogCategories.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Components/SWGCraftingComponent.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/BaselinesMessage.h"
#include "Network/Messages/Zone/DeltasMessage.h"
#include "Network/Messages/Zone/ObjControllerMessageIn.h"
#include "Network/Messages/Zone/ObjectMenuSelectMessage.h"
#include "Network/Messages/Zone/Object/CraftingMessages.h"
#include "Network/Messages/Zone/UpdateContainmentMessage.h"
#include "Network/Objects/Zone/ManufactureSchematic/ManufactureSchematicDelta.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogSWGCrafting);

namespace
{
	/** Container arrangement CraftingSessionImplementation::createManufactureSchematic transfers the MSCO into (craftingTool->transferObject(schematic, 0x4, true)) — everything else landing in the tool mid-session is the prototype. */
	constexpr int32 MscoContainerArrangement = 4;
}

void USWGCraftingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Network = Collection.InitializeDependency<USWGNetworkSubsystem>();
	Commands = Collection.InitializeDependency<USWGCommandSubsystem>();
	ObjectGraph = Collection.InitializeDependency<USWGObjectGraphSubsystem>();
	Tre = Collection.InitializeDependency<USWGTreSubsystem>();
	Radial = Collection.InitializeDependency<USWGRadialMenuSubsystem>();
	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGCraftingSubsystem::HandleMessageReceived);
	}
}

void USWGCraftingSubsystem::Deinitialize()
{
	if (Network)
	{
		Network->OnMessageReceived.Remove(MessageHandle);
	}
	if (USWGCraftingComponent* Component = BoundCraftingComponent.Get())
	{
		Component->OnChanged.RemoveAll(this);
	}
	Super::Deinitialize();
}

void USWGCraftingSubsystem::EnsureCraftingComponentBound()
{
	USWGCraftingComponent* Component = ObjectGraph ? ObjectGraph->FindComponent<USWGCraftingComponent>(ObjectGraph->GetLocalPlayerObjectId()) : nullptr;
	if (Component == BoundCraftingComponent.Get())
	{
		return;
	}
	if (USWGCraftingComponent* Old = BoundCraftingComponent.Get())
	{
		Old->OnChanged.RemoveAll(this);
	}
	BoundCraftingComponent = Component;
	if (Component)
	{
		Component->OnChanged.AddUObject(this, &USWGCraftingSubsystem::HandleCraftingComponentChanged);
		HandleCraftingComponentChanged();
	}
}

void USWGCraftingSubsystem::HandleCraftingComponentChanged()
{
	const USWGCraftingComponent* Component = BoundCraftingComponent.Get();
	if (!Component)
	{
		return;
	}
	// PLAY base9's CraftingState is the authority: the server alone knows how
	// nextCraftingStage collapses states, so this drives CurrentState rather
	// than anything we infer from which crafting messages arrived.
	const ESWGCraftingSessionState NewState = static_cast<ESWGCraftingSessionState>(FMath::Clamp(Component->CraftingState, 0, 6));
	const bool bStateChanged = NewState != CurrentState;
	CurrentState = NewState;
	if (Component->ExperimentationPoints >= 0)
	{
		ExperimentPointsTotal = FMath::Max(ExperimentPointsTotal, Component->ExperimentationPoints);
		ExperimentPointsRemaining = Component->ExperimentationPoints;
	}
	if (StationObjectId == 0)
	{
		StationObjectId = Component->ClosestCraftingStation;
	}

	if (CurrentState == ESWGCraftingSessionState::None)
	{
		if (bStateChanged)
		{
			ClearSession();
			OnSessionClosed.Broadcast();
		}
		return;
	}
	if (bStateChanged)
	{
		OnStageChanged.Broadcast();
	}
}

uint8 USWGCraftingSubsystem::NextCounter()
{
	return ClientCounter++;
}

void USWGCraftingSubsystem::ClearSession()
{
	ToolObjectId = 0;
	StationObjectId = 0;
	ManufactureSchematicId = 0;
	PrototypeId = 0;
	bAllowFactory = false;
	Schematics.Reset();
	SelectedSchematicIndex = INDEX_NONE;
	SlotRequirements.Reset();
	Slots.Reset();
	ExperimentGroups.Reset();
	ExperimentPointsTotal = 0;
	ExperimentPointsRemaining = 0;
	FailureRate = 0.f;
	TemplateChoices.Reset();
	CustomizationVarNames.Reset();
	CustomizationVarDefaults.Reset();
	CustomizationPaletteCounts.Reset();
	CurrentSchematic = FManufactureSchematicBaseline();
}

void USWGCraftingSubsystem::RebuildSlots()
{
	Slots.Reset(SlotRequirements.Num());
	const int32 OidCount = CurrentSchematic.SlotOIDs.Items.Num();
	const int32 QtyCount = CurrentSchematic.SlotQuantities.Items.Num();
	const int32 QualityCount = CurrentSchematic.SlotQualities.Items.Num();
	for (int32 Index = 0; Index < SlotRequirements.Num(); ++Index)
	{
		const FSWGDraftSlot& Requirement = SlotRequirements[Index];
		FSWGCraftingSlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Name = Requirement.StringIdName;
		if (Tre && !Requirement.StringIdFile.IsEmpty())
		{
			const FString Resolved = Tre->LookupString(Requirement.StringIdFile, Requirement.StringIdName);
			if (!Resolved.IsEmpty())
			{
				Slot.Name = Resolved;
			}
		}
		Slot.bOptional = Requirement.bOptional;
		Slot.ResourceType = Requirement.ResourceType;
		Slot.Kind = Requirement.Kind;
		Slot.RequiredQuantity = Requirement.Quantity;

		if (Index < OidCount)
		{
			Slot.FilledObjectIds.Reset(CurrentSchematic.SlotOIDs.Items[Index].Num());
			for (const uint64 Oid : CurrentSchematic.SlotOIDs.Items[Index])
			{
				Slot.FilledObjectIds.Add(static_cast<int64>(Oid));
			}
		}
		if (Index < QtyCount)
		{
			for (const int32 Quantity : CurrentSchematic.SlotQuantities.Items[Index])
			{
				Slot.FilledQuantity += Quantity;
			}
		}
		if (Index < QualityCount)
		{
			Slot.Quality = CurrentSchematic.SlotQualities.Items[Index];
		}
	}
}

// ── Message dispatch ─────────────────────────────────────────────────────

void USWGCraftingSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message)
{
	if (!Message)
	{
		return;
	}
	EnsureCraftingComponentBound();

	switch (static_cast<ESWGMessageOp>(Message->Opcode))
	{
		case ESWGMessageOp::ObjControllerMessage:
			HandleObjControllerMessage(*static_cast<const FObjControllerMessageIn*>(Message.Get()));
			break;
		case ESWGMessageOp::BaselinesMessage:
			HandleBaselinesMessage(*static_cast<const FBaselinesMessage*>(Message.Get()));
			break;
		case ESWGMessageOp::DeltasMessage:
			HandleDeltasMessage(*static_cast<const FDeltasMessage*>(Message.Get()));
			break;
		case ESWGMessageOp::UpdateContainmentMessage:
		{
			// The MSCO/prototype aren't discovered any other way — Core3 never
			// sends the MSCO a full generic baseline (see crafting-protocol.md);
			// the client learns its id purely from it landing in the tool's
			// container. Only relevant while we have a tool but no MSCO yet.
			if (ToolObjectId == 0 || ManufactureSchematicId != 0)
			{
				break;
			}
			const FUpdateContainmentMessage& Containment = *static_cast<const FUpdateContainmentMessage*>(Message.Get());
			if (Containment.ContainerId != ToolObjectId)
			{
				break;
			}
			if (static_cast<int32>(Containment.Type) == MscoContainerArrangement)
			{
				ManufactureSchematicId = Containment.ObjectId;
				UE_LOG(LogSWGCrafting, Log, TEXT("learned MSCO id %lld from containment, requesting its slots"), ManufactureSchematicId);
				ListenSlots();
			}
			else
			{
				PrototypeId = Containment.ObjectId;
			}
			break;
		}
		default:
			break;
	}
}

void USWGCraftingSubsystem::HandleObjControllerMessage(const FObjControllerMessageIn& Envelope)
{
	FSWGPacket Payload = Envelope.AsPayloadPacket();
	switch (Envelope.GetSubOp())
	{
		case ESWGObjControllerOp::CraftingSchematicList: HandleSchematicList(Payload); break;
		case ESWGObjControllerOp::CraftingIngredientSlots: HandleIngredientSlots(Payload); break;
		case ESWGObjControllerOp::CraftingSlotReply: HandleSlotReply(Payload); break;
		case ESWGObjControllerOp::CraftingAssemblyResult: HandleAssemblyResult(Payload); break;
		case ESWGObjControllerOp::CraftingExperimentResult: HandleExperimentResult(Payload); break;
		case ESWGObjControllerOp::CraftingCloseWindow: HandleCloseWindow(Payload); break;
		case ESWGObjControllerOp::CraftingDraftSlots: HandleDraftSlots(Payload); break;
		case ESWGObjControllerOp::CraftingResourceWeights: HandleResourceWeights(Payload); break;
		default: break;
	}
}

void USWGCraftingSubsystem::HandleSchematicList(FSWGPacket& Payload)
{
	FSWGCraftingSchematicListIn List;
	if (!List.Parse(Payload))
	{
		UE_LOG(LogSWGCrafting, Warning, TEXT("malformed CraftingSchematicList (0x102)"));
		return;
	}
	ClearSession();
	ToolObjectId = List.ToolId;
	StationObjectId = List.StationId;
	CurrentState = ESWGCraftingSessionState::ChoosingSchematic;
	for (const FSWGCraftingSchematicEntry& Entry : List.Schematics)
	{
		FSWGCraftingSchematicOption& Option = Schematics.AddDefaulted_GetRef();
		Option.SchematicCrc = Entry.SchematicCrc;
		Option.ToolTab = Entry.ToolTab;
		Option.TemplatePath = Tre ? Tre->ResolveTemplatePath(static_cast<uint32>(Entry.SchematicCrc)) : FString();
		FString Display = FPaths::GetBaseFilename(Option.TemplatePath);
		Display.ReplaceInline(TEXT("_"), TEXT(" "));
		Option.Name = Display.IsEmpty() ? FString::Printf(TEXT("0x%08X"), Entry.SchematicCrc) : Display;
	}
	UE_LOG(LogSWGCrafting, Log, TEXT("session started: tool=%lld station=%lld %d schematic(s)"), ToolObjectId, StationObjectId, Schematics.Num());
	OnSessionStarted.Broadcast();
}

void USWGCraftingSubsystem::HandleIngredientSlots(FSWGPacket& Payload)
{
	FSWGCraftingIngredientSlotsIn Slots2;
	if (!Slots2.Parse(Payload))
	{
		UE_LOG(LogSWGCrafting, Warning, TEXT("malformed CraftingIngredientSlots (0x103)"));
		return;
	}
	ManufactureSchematicId = Slots2.ManufactureSchematicId;
	PrototypeId = Slots2.PrototypeId;
	bAllowFactory = Slots2.AllowFactory != 0;
	SlotRequirements = Slots2.Slots;
	RebuildSlots();
	OnSlotsChanged.Broadcast();
}

void USWGCraftingSubsystem::HandleSlotReply(FSWGPacket& Payload)
{
	FSWGCraftingStatusIn Status;
	if (!Status.Parse(Payload))
	{
		UE_LOG(LogSWGCrafting, Warning, TEXT("malformed CraftingSlotReply (0x10C)"));
		return;
	}
	switch (static_cast<ESWGCraftingSlotReplySubType>(Status.SubType))
	{
		case ESWGCraftingSlotReplySubType::AddIngredientResult:
			OnSlotResult.Broadcast(static_cast<ESWGCraftingSlotResult>(Status.Value));
			if (Status.Value == static_cast<int32>(ESWGCraftingSlotResult::OK))
			{
				// Slot fill state lives on MSCO base7, only refreshed by re-asking — no dedicated slot delta is decoded yet (see crafting-protocol.md carry-forward).
				ListenSlots();
			}
			break;
		case ESWGCraftingSlotReplySubType::RemoveIngredientOk:
			OnSlotResult.Broadcast(ESWGCraftingSlotResult::OK);
			ListenSlots();
			break;
		case ESWGCraftingSlotReplySubType::ToolStartFailed:
			UE_LOG(LogSWGCrafting, Warning, TEXT("tool failed to start a session"));
			ClearSession();
			CurrentState = ESWGCraftingSessionState::None;
			OnSessionClosed.Broadcast();
			break;
		case ESWGCraftingSlotReplySubType::HopperBracket:
			// Client-side adaptation for a second Core3 quirk on the same
			// Continue-from-Assembled path (see the CraftingAssemblyResult
			// case below for the Customizing one): nextCraftingStage's
			// state==4 branch calls finishStage2, whose PLAY9 delta writes
			// its CraftingState field via a raw insertShort(5) instead of
			// the startUpdate() wrapper every other field uses — that
			// skips whatever increments the message's own field-update
			// count, so our ReadDeltaUpdates loop (which trusts that count)
			// stops one field early and never sees CraftingState=6, even
			// though the bytes are right there in the packet. finishStage2
			// is the only sender of this HopperBracket reply while a
			// session is still Assembled, so its arrival there is a
			// reliable stand-in for the PLAY9 confirmation that never comes.
			if (CurrentState == ESWGCraftingSessionState::Assembled)
			{
				CurrentState = ESWGCraftingSessionState::ReadyToFinish;
				OnStageChanged.Broadcast();
			}
			break;
		default:
			break; // CustomizeApplied: no client state change needed
	}
}

void USWGCraftingSubsystem::HandleAssemblyResult(FSWGPacket& Payload)
{
	FSWGCraftingStatusIn Status;
	if (Status.Parse(Payload))
	{
		if (CurrentState == ESWGCraftingSessionState::Customizing)
		{
			// Client-side adaptation for a Core3 server quirk (not fixed
			// server-side — see crafting-plan.md/project_crafting memory):
			// nextCraftingStage's state==5 branch only calls finishStage1,
			// whose own PLAY9 delta captures CraftingState BEFORE it
			// internally advances to 6, and nothing afterward announces the
			// advance. This 0x1BE is the only signal we ever get that
			// Continue succeeded from Customizing; Value here is a fixed
			// marker (always 4), not a real assembly result, so treat it as
			// a stage advance instead of feeding it to OnAssemblyResult.
			CurrentState = ESWGCraftingSessionState::ReadyToFinish;
			OnStageChanged.Broadcast();
		}
		else
		{
			OnAssemblyResult.Broadcast(static_cast<ESWGCraftingResult>(FMath::Clamp(Status.Value, 0, 8)));
		}
	}
}

void USWGCraftingSubsystem::HandleExperimentResult(FSWGPacket& Payload)
{
	FSWGCraftingStatusIn Status;
	if (Status.Parse(Payload))
	{
		OnExperimentResult.Broadcast(static_cast<ESWGCraftingResult>(FMath::Clamp(Status.Value, 0, 8)));
	}
}

void USWGCraftingSubsystem::HandleCloseWindow(FSWGPacket& Payload)
{
	FSWGCraftingCloseWindowIn Close;
	Close.Parse(Payload);
	ClearSession();
	CurrentState = ESWGCraftingSessionState::None;
	OnSessionClosed.Broadcast();
}

void USWGCraftingSubsystem::HandleDraftSlots(FSWGPacket& Payload)
{
	FSWGCraftingDraftSlotsIn Slots3;
	if (Slots3.Parse(Payload))
	{
		UE_LOG(LogSWGCrafting, Log, TEXT("draft slots preview: crc=0x%08X, %d slot(s)"), Slots3.SchematicCrc, Slots3.Slots.Num());
		DraftSlotsPreview.Add(static_cast<int32>(Slots3.SchematicCrc), Slots3);
		OnDraftPreviewChanged.Broadcast();
	}
	else
	{
		UE_LOG(LogSWGCrafting, Warning, TEXT("malformed CraftingDraftSlots (0x1BF), payload %d bytes"), Payload.GetSize());
	}
}

void USWGCraftingSubsystem::HandleResourceWeights(FSWGPacket& Payload)
{
	FSWGCraftingResourceWeightsIn Weights;
	if (Weights.Parse(Payload))
	{
		UE_LOG(LogSWGCrafting, Log, TEXT("resource weights preview: crc=0x%08X, %d weight(s)"), Weights.SchematicCrc, Weights.Weights.Num());
		ResourceWeightsPreview.Add(static_cast<int32>(Weights.SchematicCrc), Weights);
		OnDraftPreviewChanged.Broadcast();
	}
	else
	{
		UE_LOG(LogSWGCrafting, Warning, TEXT("malformed CraftingResourceWeights (0x207), payload %d bytes"), Payload.GetSize());
	}
}

void USWGCraftingSubsystem::HandleBaselinesMessage(const FBaselinesMessage& Baselines)
{
	if (Baselines.GetObjectType() != ESWGObjectType::MSCO || Baselines.ObjectId != ManufactureSchematicId || ManufactureSchematicId == 0)
	{
		return;
	}
	FSWGPacket Packet = Baselines.AsPayloadPacket();
	switch (Baselines.BaselineType)
	{
		case 3: SWGManufactureSchematicBaselineParser::ParseBase3(Packet, CurrentSchematic); OnSlotsChanged.Broadcast(); break;
		case 6: SWGManufactureSchematicBaselineParser::ParseBase6(Packet, CurrentSchematic); break;
		case 7:
			SWGManufactureSchematicBaselineParser::ParseBase7(Packet, CurrentSchematic);
			ExperimentGroups.Reset(CurrentSchematic.ExperimentGroupTitles.Num());
			for (int32 Index = 0; Index < CurrentSchematic.ExperimentGroupTitles.Num(); ++Index)
			{
				FSWGCraftingExperimentGroup& Group = ExperimentGroups.AddDefaulted_GetRef();
				Group.Title = CurrentSchematic.ExperimentGroupTitles[Index];
				Group.CurrentPercent = CurrentSchematic.ExperimentCurrentPercent.IsValidIndex(Index) ? CurrentSchematic.ExperimentCurrentPercent[Index] : 0.f;
				Group.MaxPercent = CurrentSchematic.ExperimentMax.IsValidIndex(Index) ? CurrentSchematic.ExperimentMax[Index] : 1.f;
			}
			RebuildSlots();
			OnSlotsChanged.Broadcast();
			break;
		default:
			break;
	}
}

void USWGCraftingSubsystem::HandleDeltasMessage(const FDeltasMessage& Deltas)
{
	if (Deltas.GetObjectType() != ESWGObjectType::MSCO || Deltas.ObjectId != ManufactureSchematicId || ManufactureSchematicId == 0)
	{
		return;
	}
	FSWGPacket Packet = Deltas.AsPayloadPacket();
	switch (Deltas.DeltaType)
	{
		case 3:
		{
			FManufactureSchematicDelta Delta;
			SWGManufactureSchematicDeltaParser::ParseDelta3(Packet, Delta, Deltas.UpdateCount);
			if (Delta.Complexity.IsSet()) { /* not surfaced separately yet — read via GetSchematicBaseline() if needed later */ }
			if (Delta.Name.IsSet()) { CurrentSchematic.CustomName = *Delta.Name; }
			if (Delta.Condition.IsSet()) { CurrentSchematic.ManufactureLimit = *Delta.Condition; }
			OnSlotsChanged.Broadcast();
			break;
		}
		case 7:
		{
			FManufactureSchematicDelta Delta;
			SWGManufactureSchematicDeltaParser::ParseDelta7(Packet, Delta, Deltas.UpdateCount);
			if (Delta.GroupTitles.IsSet())
			{
				TArray<FString> Titles;
				for (const auto& Group : ExperimentGroups) { Titles.Add(Group.Title); }
				ApplyIndexedListChanges(*Delta.GroupTitles, Titles);
				ExperimentGroups.SetNum(Titles.Num());
				for (int32 Index = 0; Index < Titles.Num(); ++Index) { ExperimentGroups[Index].Title = Titles[Index]; }
			}
			if (Delta.GroupCurrentPercent.IsSet())
			{
				TArray<float> Percents;
				for (const auto& Group : ExperimentGroups) { Percents.Add(Group.CurrentPercent); }
				ApplyIndexedListChanges(*Delta.GroupCurrentPercent, Percents);
				for (int32 Index = 0; Index < Percents.Num() && Index < ExperimentGroups.Num(); ++Index) { ExperimentGroups[Index].CurrentPercent = Percents[Index]; }
			}
			if (Delta.GroupMaxPercent.IsSet())
			{
				TArray<float> MaxPercents;
				for (const auto& Group : ExperimentGroups) { MaxPercents.Add(Group.MaxPercent); }
				ApplyIndexedListChanges(*Delta.GroupMaxPercent, MaxPercents);
				for (int32 Index = 0; Index < MaxPercents.Num() && Index < ExperimentGroups.Num(); ++Index) { ExperimentGroups[Index].MaxPercent = MaxPercents[Index]; }
			}
			if (Delta.FailureRate.IsSet()) { FailureRate = *Delta.FailureRate; }
			if (Delta.Templates.IsSet()) { ApplyIndexedListChanges(*Delta.Templates, TemplateChoices); }
			if (Delta.CustomizationVarNames.IsSet()) { ApplyIndexedListChanges(*Delta.CustomizationVarNames, CustomizationVarNames); }
			if (Delta.CustomizationVarDefaults.IsSet()) { ApplyIndexedListChanges(*Delta.CustomizationVarDefaults, CustomizationVarDefaults); }
			if (Delta.CustomizationPaletteCounts.IsSet()) { ApplyIndexedListChanges(*Delta.CustomizationPaletteCounts, CustomizationPaletteCounts); }
			OnSlotsChanged.Broadcast();
			break;
		}
		default:
			break;
	}
}

// ── Actions ──────────────────────────────────────────────────────────────

bool USWGCraftingSubsystem::StartSession(int64 ToolOrStationObjectId)
{
	return Commands && ToolOrStationObjectId != 0 && Commands->SendCommand(TEXT("requestCraftingSession"), ToolOrStationObjectId) != 0;
}

bool USWGCraftingSubsystem::SelectSchematic(int32 Index)
{
	if (!Commands || !Schematics.IsValidIndex(Index))
	{
		return false;
	}
	if (Commands->SendCommand(TEXT("selectDraftSchematic"), 0, FString::FromInt(Index)) == 0)
	{
		return false;
	}
	SelectedSchematicIndex = Index;
	return true;
}

bool USWGCraftingSubsystem::ListenSlots()
{
	return Commands && ManufactureSchematicId != 0
		&& Commands->SendCommand(TEXT("synchronizedUiListen"), ManufactureSchematicId, TEXT("0")) != 0;
}

bool USWGCraftingSubsystem::RequestDraftPreview(const TArray<int32>& SchematicCrcs)
{
	if (!Commands)
	{
		return false;
	}
	TArray<int32> SlotsNeeded;
	TArray<int32> WeightsNeeded;
	for (const int32 Crc : SchematicCrcs)
	{
		if (!DraftSlotsPreview.Contains(Crc)) { SlotsNeeded.Add(Crc); }
		if (!ResourceWeightsPreview.Contains(Crc)) { WeightsNeeded.Add(Crc); }
	}
	bool bSentAny = false;
	if (!SlotsNeeded.IsEmpty())
	{
		// RequestDraftSlotsBatchCommand reads (id, crc) pairs and only uses the first of
		// each pair as the schematic to look up — see crafting-protocol.md. Duplicating the
		// CRC into both slots keeps this simple and matches the wire's own "echo crc twice" idiom.
		FString Args;
		for (const int32 Crc : SlotsNeeded)
		{
			Args += FString::Printf(TEXT("%u %u "), static_cast<uint32>(Crc), static_cast<uint32>(Crc));
		}
		UE_LOG(LogSWGCrafting, Log, TEXT("requestDraftSlotsBatch args (%d schematic(s)): %s"), SlotsNeeded.Num(), *Args);
		bSentAny |= Commands->SendCommand(TEXT("requestDraftSlotsBatch"), 0, Args) != 0;
	}
	if (!WeightsNeeded.IsEmpty())
	{
		FString Args;
		for (const int32 Crc : WeightsNeeded)
		{
			Args += FString::Printf(TEXT("%u "), static_cast<uint32>(Crc));
		}
		UE_LOG(LogSWGCrafting, Log, TEXT("requestResourceWeightsBatch args (%d schematic(s)): %s"), WeightsNeeded.Num(), *Args);
		bSentAny |= Commands->SendCommand(TEXT("requestResourceWeightsBatch"), 0, Args) != 0;
	}
	return bSentAny;
}

bool USWGCraftingSubsystem::AddIngredient(int64 IngredientObjectId, int32 Slot)
{
	if (!Network || !ObjectGraph || IngredientObjectId == 0)
	{
		return false;
	}
	FSWGCraftingAddIngredientMessage Msg(ObjectGraph->GetLocalPlayerObjectId(), IngredientObjectId, Slot, NextCounter());
	Network->SendMessage(Msg.Serialize());
	return true;
}

bool USWGCraftingSubsystem::RemoveIngredient(int32 Slot, int64 IngredientObjectId)
{
	if (!Network || !ObjectGraph)
	{
		return false;
	}
	FSWGCraftingRemoveIngredientMessage Msg(ObjectGraph->GetLocalPlayerObjectId(), Slot, IngredientObjectId, NextCounter());
	Network->SendMessage(Msg.Serialize());
	return true;
}

bool USWGCraftingSubsystem::Assemble()
{
	return Commands && Commands->SendCommand(TEXT("nextCraftingStage"), 0, FString::FromInt(NextCounter())) != 0;
}

bool USWGCraftingSubsystem::Experiment(const TArray<FSWGCraftingExperimentRow>& Rows)
{
	if (!Network || !ObjectGraph || CurrentState != ESWGCraftingSessionState::Experimenting || Rows.IsEmpty())
	{
		return false;
	}
	FSWGCraftingExperimentMessage Msg(ObjectGraph->GetLocalPlayerObjectId(), NextCounter(), Rows);
	Network->SendMessage(Msg.Serialize());
	return true;
}

bool USWGCraftingSubsystem::Customize(const FString& Name, uint8 TemplateChoice, int32 SchematicCount, const TArray<FSWGCraftingCustomizationEdit>& Edits)
{
	if (!Network || !ObjectGraph)
	{
		return false;
	}
	FSWGCraftingCustomizeMessage Msg(ObjectGraph->GetLocalPlayerObjectId(), Name, TemplateChoice, SchematicCount, Edits);
	Network->SendMessage(Msg.Serialize());
	return true;
}

bool USWGCraftingSubsystem::CreatePrototype(bool bPractice)
{
	if (!Commands || CurrentState != ESWGCraftingSessionState::ReadyToFinish)
	{
		return false;
	}
	return Commands->SendCommand(TEXT("createPrototype"), 0, FString::Printf(TEXT("%d %d"), NextCounter(), bPractice ? 0 : 1)) != 0;
}

bool USWGCraftingSubsystem::CreateManufactureSchematic()
{
	if (!Commands || CurrentState != ESWGCraftingSessionState::ReadyToFinish || !bAllowFactory)
	{
		return false;
	}
	return Commands->SendCommand(TEXT("createManfSchematic"), 0, FString::FromInt(NextCounter())) != 0;
}

bool USWGCraftingSubsystem::Cancel()
{
	return Commands && Commands->SendCommand(TEXT("cancelCraftingSession"), 0) != 0;
}

bool USWGCraftingSubsystem::RetrieveOutput(int64 ToolObjId)
{
	if (!Network || !Radial || ToolObjId == 0)
	{
		return false;
	}
	const int32 RadialId = Radial->ResolveRadialId(TEXT("SERVER_ITEM_OPTIONS"));
	if (RadialId == INDEX_NONE)
	{
		return false;
	}
	FObjectMenuSelectMessage Select;
	Select.ObjectId = ToolObjId;
	Select.RadialId = static_cast<uint8>(RadialId);
	Network->SendMessage(Select.Serialize());
	return true;
}

// ── Testing without a tool ───────────────────────────────────────────────

void USWGCraftingSubsystem::InjectSchematics(const TArray<FSWGCraftingSchematicOption>& InSchematics)
{
	ClearSession();
	Schematics = InSchematics;
	CurrentState = ESWGCraftingSessionState::ChoosingSchematic;
	OnSessionStarted.Broadcast();
}

void USWGCraftingSubsystem::InjectSlotsAndExperiment(const TArray<FSWGCraftingSlot>& InSlots, const TArray<FSWGCraftingExperimentGroup>& InGroups, int32 PointsTotal, int32 PointsRemaining)
{
	Slots = InSlots;
	ExperimentGroups = InGroups;
	ExperimentPointsTotal = PointsTotal;
	ExperimentPointsRemaining = PointsRemaining;
	CurrentState = ESWGCraftingSessionState::Experimenting;
	OnSlotsChanged.Broadcast();
	OnStageChanged.Broadcast();
}

void USWGCraftingSubsystem::InjectAssemblySlots(const TArray<FSWGCraftingSlot>& InSlots)
{
	Slots = InSlots;
	ExperimentGroups.Reset();
	CurrentState = ESWGCraftingSessionState::Assembling;
	OnSlotsChanged.Broadcast();
	OnStageChanged.Broadcast();
}
