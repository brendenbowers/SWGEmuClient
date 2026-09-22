#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "Subsystems/SWGExamineSubsystem.h"
#include "Subsystems/SWGItemTransferSubsystem.h"
#include "Subsystems/SWGNetworkSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGTargetSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Network/Messages/SWGMessageOp.h"
#include "Network/Messages/Zone/ObjControllerMessageIn.h"
#include "Network/Messages/Zone/Object/ObjectMenuResponseIn.h"
#include "Network/Messages/Zone/ObjectMenuSelectMessage.h"
#include "Components/SWGTerminalComponent.h"
#include "Objects/Creature/SWGCreature.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Objects/SWGObject.h"
#include "Objects/Tangible/SWGItem.h"
#include "Objects/World/SWGInstallation.h"
#include "TRE/SWGDataTableReader.h"
#include "TRE/SWGIffReader.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWGRadial, Log, All);

// swg.RadialMenu [objectId] — opens the radial menu for the current target
// (or the given object) at screen centre; a right-click without the mouse.
static FAutoConsoleCommandWithWorldAndArgs GSWGRadialMenuCommand(
	TEXT("swg.RadialMenu"),
	TEXT("Request the radial menu for the current target, or for the object id given."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		// The editor console hands over the editor world; the menu lives in the play world.
		if (GEngine && (!World || !World->IsGameWorld()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
					break;
				}
			}
		}

		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGRadialMenuSubsystem* Radial = GameInstance ? GameInstance->GetSubsystem<USWGRadialMenuSubsystem>() : nullptr;
		USWGTargetSubsystem* Targets = GameInstance ? GameInstance->GetSubsystem<USWGTargetSubsystem>() : nullptr;
		if (!Radial)
		{
			return;
		}

		const int64 ObjectId = Args.Num() > 0 ? FCString::Atoi64(*Args[0]) : (Targets ? Targets->GetTargetId() : 0);
		int32 ViewportX = 0, ViewportY = 0;
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			PlayerController->GetViewportSize(ViewportX, ViewportY);
		}
		if (!Radial->RequestMenu(ObjectId, FVector2D(ViewportX * 0.5f, ViewportY * 0.5f)))
		{
			UE_LOG(LogSWGRadial, Warning, TEXT("swg.RadialMenu: no target (pass an object id)"));
		}
	}));

// swg.RadialSelect <radialId> [objectId] — picks an option from the last menu
// received for the target (or given object), as clicking the row would.
static FAutoConsoleCommandWithWorldAndArgs GSWGRadialSelectCommand(
	TEXT("swg.RadialSelect"),
	TEXT("Pick a radial option by id from the last menu received for the current target (or the object id given)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (GEngine && (!World || !World->IsGameWorld()))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
					break;
				}
			}
		}

		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USWGRadialMenuSubsystem* Radial = GameInstance ? GameInstance->GetSubsystem<USWGRadialMenuSubsystem>() : nullptr;
		USWGTargetSubsystem* Targets = GameInstance ? GameInstance->GetSubsystem<USWGTargetSubsystem>() : nullptr;
		if (!Radial || Args.IsEmpty())
		{
			UE_LOG(LogSWGRadial, Warning, TEXT("usage: swg.RadialSelect <radialId> [objectId]"));
			return;
		}

		const int64 ObjectId = Args.Num() > 1 ? FCString::Atoi64(*Args[1]) : (Targets ? Targets->GetTargetId() : 0);
		Radial->SelectOption(ObjectId, FCString::Atoi(*Args[0]));
	}));

namespace
{
	FString MakeStandardLabelId(const FString& Caption)
	{
		static const TMap<FString, FString> LabelOverrides = {
			{ TEXT("VEHICLE_GENERATE"), TEXT("control_call") },
			{ TEXT("VEHICLE_STORE"), TEXT("control_store") },
			{ TEXT("PET_STORE"), TEXT("control_store") },
		};
		if (const FString* Override = LabelOverrides.Find(Caption.ToUpper()))
		{
			return TEXT("@ui_radial:") + *Override;
		}
		return TEXT("@ui_radial:") + Caption.ToLower();
	}
}

void USWGRadialMenuSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Network = Collection.InitializeDependency<USWGNetworkSubsystem>();
	ObjectGraph = Collection.InitializeDependency<USWGObjectGraphSubsystem>();
	Commands = Collection.InitializeDependency<USWGCommandSubsystem>();
	Tre = Collection.InitializeDependency<USWGTreSubsystem>();

	if (Network)
	{
		MessageHandle = Network->OnMessageReceived.AddUObject(this, &USWGRadialMenuSubsystem::HandleMessageReceived);
	}
}

void USWGRadialMenuSubsystem::Deinitialize()
{
	if (Network)
	{
		Network->OnMessageReceived.Remove(MessageHandle);
	}
	Super::Deinitialize();
}

bool USWGRadialMenuSubsystem::RequestMenu(int64 ObjectId, FVector2D ScreenPosition)
{
	if (!Network || !ObjectGraph || ObjectId == 0)
	{
		return false;
	}

	const int64 PlayerId = ObjectGraph->GetLocalPlayerObjectId();
	if (PlayerId == 0)
	{
		return false;
	}

	PendingObjectId = ObjectId;
	PendingScreenPosition = ScreenPosition;

	FObjectMenuRequest Request(PlayerId, ObjectId, ++NextCounter);

	// Some options (currently just "Use" — see its rule's comment in
	// GetClientRadialRules) need to be pre-seeded in the request itself, not
	// just offered client-side after the response comes back.
	int32 NextSeedIndex = 1;
	for (const FSWGClientRadialRule& Rule : GetClientRadialRules())
	{
		if (Rule.RadialId != INDEX_NONE && Rule.bSeedInRequest && Rule.ShouldOffer(ObjectId))
		{
			FSWGRadialMenuEntry& Seeded = Request.ClientItems.AddDefaulted_GetRef();
			Seeded.Index = static_cast<uint8>(NextSeedIndex++);
			Seeded.ParentIndex = 0;
			Seeded.RadialId = static_cast<uint8>(Rule.RadialId);
			Seeded.Callback = 3;
		}
	}

	Network->SendMessage(Request.Serialize());

	UE_LOG(LogSWGRadial, Verbose, TEXT("requested menu for %lld (counter %u)"), ObjectId, Request.Counter);
	return true;
}

bool USWGRadialMenuSubsystem::IsMissionTerminal(int64 ObjectId) const
{
	if (!ObjectGraph)
	{
		return false;
	}
	AActor* Actor = ObjectGraph->FindActor(ObjectId);
	if (!Actor)
	{
		return false;
	}

	// FSWGTerminalSpawnHandler already classified this at spawn time — see
	// USWGTerminalComponent for why that beats re-deriving the template path
	// (and its SWGObjectCRC-is-zero-for-.ws-statics wrinkle) on every click.
	const USWGTerminalComponent* Terminal = Actor->FindComponentByClass<USWGTerminalComponent>();
	const bool bIsMission = Terminal && Terminal->TerminalType == ESWGTerminalType::Mission;
	UE_LOG(LogSWGRadial, Verbose, TEXT("IsMissionTerminal: %lld terminal=%s -> %s"), ObjectId,
		Terminal ? TEXT("yes") : TEXT("no"), bIsMission ? TEXT("true") : TEXT("false"));
	return bIsMission;
}

void USWGRadialMenuSubsystem::SelectOption(int64 ObjectId, int32 RadialId)
{
	const FSWGRadialMenu* Menu = ReceivedMenus.Find(ObjectId);
	const FSWGRadialMenuItem* Item = Menu
		? Menu->Items.FindByPredicate([RadialId](const FSWGRadialMenuItem& Candidate) { return Candidate.RadialId == RadialId; })
		: nullptr;
	if (!Item)
	{
		UE_LOG(LogSWGRadial, Warning, TEXT("SelectOption: no option %d in the menu for %lld"), RadialId, ObjectId);
		return;
	}

	// Client-drawn options (Equip/Unequip/Use's equip-toggle and mission-
	// terminal special cases, Examine, ...) handle their own pick before
	// anything server-related — see GetClientRadialRules for why each one
	// does what it does. A rule with no OnSelected, or one that returns
	// false, falls through to the generic paths below unchanged.
	for (const FSWGClientRadialRule& Rule : GetClientRadialRules())
	{
		if (Rule.RadialId == RadialId && Rule.OnSelected && Rule.OnSelected(ObjectId))
		{
			return;
		}
	}

	if (Item->bServerHandled)
	{
		if (Network)
		{
			FObjectMenuSelectMessage Select;
			Select.ObjectId = ObjectId;
			Select.RadialId = static_cast<uint8>(RadialId);
			Network->SendMessage(Select.Serialize());
			UE_LOG(LogSWGRadial, Log, TEXT("selected server option %d on %lld"), RadialId, ObjectId);
		}
		return;
	}

	const FSWGDataTableData* Table = GetRadialTable();
	const FString Command = Table ? Table->GetCell(RadialId, TEXT("command")) : FString();
	if (Command.IsEmpty())
	{
		// The remaining client options open windows retail draws itself; nothing to send yet.
		UE_LOG(LogSWGRadial, Log, TEXT("client option %d on %lld has no command — not implemented"), RadialId, ObjectId);
		return;
	}

	if (Commands)
	{
		Commands->SendCommand(Command, ObjectId);
	}
}

void USWGRadialMenuSubsystem::HandleMessageReceived(TSharedPtr<FSWGNetMessage> Message)
{
	if (!Message || Message->Opcode != static_cast<uint32>(ESWGMessageOp::ObjControllerMessage))
	{
		return;
	}

	const FObjControllerMessageIn& Envelope = *static_cast<const FObjControllerMessageIn*>(Message.Get());
	if (Envelope.GetSubOp() != ESWGObjControllerOp::ObjectMenuResponse)
	{
		return;
	}

	FSWGPacket Payload = Envelope.AsPayloadPacket();
	FObjectMenuResponseIn Response;
	if (!Response.Parse(Payload))
	{
		UE_LOG(LogSWGRadial, Warning, TEXT("malformed ObjectMenuResponse (%d bytes)"), Envelope.RawPayload.Num());
		return;
	}

	FSWGRadialMenu Menu;
	Menu.ObjectId = static_cast<int64>(Response.TargetId);
	Menu.ScreenPosition = (Menu.ObjectId == PendingObjectId) ? PendingScreenPosition : FVector2D::ZeroVector;

	for (const FSWGRadialMenuEntry& Entry : Response.Items)
	{
		FSWGRadialMenuItem& Item = Menu.Items.AddDefaulted_GetRef();
		Item.Index = Entry.Index;
		Item.ParentIndex = Entry.ParentIndex;
		Item.RadialId = Entry.RadialId;
		Item.Label = ResolveLabel(Entry);
		Item.bServerHandled = Entry.NotifiesServer();
	}
	AppendClientDefaults(Menu.ObjectId, Menu.Items);

	UE_LOG(LogSWGRadial, Log, TEXT("menu for %lld: %d option(s) (%d from server)"), Menu.ObjectId, Menu.Items.Num(), Response.Items.Num());
	for (const FSWGRadialMenuItem& Item : Menu.Items)
	{
		UE_LOG(LogSWGRadial, Verbose, TEXT("  [%d] parent %d radial %d '%s'%s"),
			Item.Index, Item.ParentIndex, Item.RadialId, *Item.Label.ToString(), Item.bServerHandled ? TEXT(" (server)") : TEXT(""));
	}

	ReceivedMenus.Add(Menu.ObjectId, Menu);
	OnMenuReceived.Broadcast(Menu);
}

void USWGRadialMenuSubsystem::AppendClientDefaults(int64 ObjectId, TArray<FSWGRadialMenuItem>& Items) const
{
	const FSWGDataTableData* Table = GetRadialTable();
	if (!Table)
	{
		return;
	}

	auto AlreadyOffered = [&Items](int32 RadialId)
	{
		return Items.ContainsByPredicate([RadialId](const FSWGRadialMenuItem& Item) { return Item.RadialId == RadialId; });
	};

	int32 NextIndex = 0;
	for (const FSWGRadialMenuItem& Item : Items)
	{
		NextIndex = FMath::Max(NextIndex, Item.Index);
	}

	for (const FSWGClientRadialRule& Rule : GetClientRadialRules())
	{
		if (Rule.RadialId == INDEX_NONE || AlreadyOffered(Rule.RadialId) || !Rule.ShouldOffer(ObjectId))
		{
			continue;
		}
		FSWGRadialMenuItem& Item = Items.AddDefaulted_GetRef();
		Item.Index = ++NextIndex;
		Item.ParentIndex = 0;
		Item.RadialId = Rule.RadialId;
		const FString Caption = Table->GetCell(Rule.RadialId, TEXT("caption"));
		Item.Label = FText::FromString(Tre ? Tre->ResolveStringId(MakeStandardLabelId(Caption)) : Caption);
		Item.bServerHandled = false;
	}
}

bool USWGRadialMenuSubsystem::ToggleEquip(int64 ObjectId) const
{
	USWGItemTransferSubsystem* Transfer = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGItemTransferSubsystem>() : nullptr;
	if (!Transfer || !Transfer->IsEquippable(ObjectId))
	{
		return false;
	}
	if (Transfer->IsEquipped(ObjectId))
	{
		Transfer->UnequipItem(ObjectId);
	}
	else
	{
		Transfer->EquipItem(ObjectId);
	}
	return true;
}

const TArray<USWGRadialMenuSubsystem::FSWGClientRadialRule>& USWGRadialMenuSubsystem::GetClientRadialRules() const
{
	if (bBuiltClientRadialRules)
	{
		return ClientRadialRules;
	}
	bBuiltClientRadialRules = true;

	// Row index == radial id is Core3's own convention (RadialOptions.h:
	// "Do not modify this list, it matches datatables/player/radial_menu.iff"),
	// and that enum's names — resolved here by ResolveRadialId(Name), against
	// the table's own "caption" column (see ResolveRadialId's comment) — are
	// the actual source of truth, avoiding hand-counting the id ourselves (a
	// wrong "60 vs 61" count is exactly what happened before this).
	const int32 RadialCombatAttack = ResolveRadialId(TEXT("COMBAT_ATTACK"));
	const int32 RadialItemEquip = ResolveRadialId(TEXT("ITEM_EQUIP"));
	const int32 RadialItemUnequip = ResolveRadialId(TEXT("ITEM_UNEQUIP"));
	const int32 RadialItemUse = ResolveRadialId(TEXT("ITEM_USE"));
	const int32 RadialVehicleGenerate = ResolveRadialId(TEXT("VEHICLE_GENERATE"));
	const int32 RadialExamine = ResolveRadialId(TEXT("EXAMINE"));
	const int32 RadialItemDestroy = ResolveRadialId(TEXT("ITEM_DESTROY"));

	// Attack — a live, non-self creature. No OnSelected: falls through to
	// the generic table "command" dispatch, same as it always has.
	ClientRadialRules.Add(FSWGClientRadialRule{ RadialCombatAttack,
		[this](int64 ObjectId)
		{
			const bool bIsSelf = ObjectGraph && ObjectGraph->GetLocalPlayerObjectId() == ObjectId;
			const ASWGCreature* Creature = ObjectGraph ? Cast<ASWGCreature>(ObjectGraph->FindActor(ObjectId)) : nullptr;
			return Creature && !bIsSelf;
		},
		false, nullptr });

	// Equip / Unequip — wearables and weapons; Core3's WearableObjectMenuComponent
	// does nothing with ITEM_USE, so this is entirely client-local (a
	// transferItem* command), same action either direction.
	ClientRadialRules.Add(FSWGClientRadialRule{ RadialItemEquip,
		[this](int64 ObjectId)
		{
			USWGItemTransferSubsystem* Transfer = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGItemTransferSubsystem>() : nullptr;
			return Transfer && Transfer->IsEquippable(ObjectId) && !Transfer->IsEquipped(ObjectId);
		},
		false, [this](int64 ObjectId) { return ToggleEquip(ObjectId); } });

	ClientRadialRules.Add(FSWGClientRadialRule{ RadialItemUnequip,
		[this](int64 ObjectId)
		{
			USWGItemTransferSubsystem* Transfer = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGItemTransferSubsystem>() : nullptr;
			return Transfer && Transfer->IsEquippable(ObjectId) && Transfer->IsEquipped(ObjectId);
		},
		false, [this](int64 ObjectId) { return ToggleEquip(ObjectId); } });

	ClientRadialRules.Add(FSWGClientRadialRule{ RadialItemUse,
		[this](int64 ObjectId)
		{
			const AActor* Actor = ObjectGraph ? ObjectGraph->FindActor(ObjectId) : nullptr;
			return Actor && (Actor->IsA<ASWGItem>() || Actor->IsA<ASWGInstallation>());
		},
		true,
		[this](int64 ObjectId)
		{
			if (IsMissionTerminal(ObjectId))
			{
				OnMissionTerminalUsed.Broadcast(ObjectId);
				return true;
			}
			return ToggleEquip(ObjectId);
		} });

	ClientRadialRules.Add(FSWGClientRadialRule{ RadialVehicleGenerate,
		[this](int64 ObjectId)
		{
			if (!ObjectGraph || !Tre || !ObjectGraph->IsOwnedByLocalPlayer(ObjectId))
			{
				return false;
			}
			const uint32 Crc = ObjectGraph->FindObjectCrc(ObjectId);
			return Crc != 0 && Tre->ResolveTemplatePath(Crc).Contains(TEXT("object/intangible/vehicle/"));
		},
		false,
		[this, RadialVehicleGenerate](int64 ObjectId)
		{
			if (Network)
			{
				FObjectMenuSelectMessage Select;
				Select.ObjectId = ObjectId;
				Select.RadialId = static_cast<uint8>(RadialVehicleGenerate);
				Network->SendMessage(Select.Serialize());
				UE_LOG(LogSWGRadial, Log, TEXT("selected client-drawn option %d (Call) on %lld"), RadialVehicleGenerate, ObjectId);
			}
			return true;
		} });

	// Examine — always offered, handled entirely client-side.
	ClientRadialRules.Add(FSWGClientRadialRule{ RadialExamine,
		[](int64) { return true; },
		false,
		[this](int64 ObjectId)
		{
			if (USWGExamineSubsystem* Examine = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGExamineSubsystem>() : nullptr)
			{
				Examine->RequestExamine(ObjectId);
			}
			return true;
		} });

	ClientRadialRules.Add(FSWGClientRadialRule{ RadialItemDestroy,
		[this](int64 ObjectId) { return ObjectGraph && ObjectGraph->IsOwnedByLocalPlayer(ObjectId); },
		false, nullptr });

	return ClientRadialRules;
}

FText USWGRadialMenuSubsystem::ResolveLabel(const FSWGRadialMenuEntry& Entry) const
{
	FString Text = Entry.Text;
	if (Text.IsEmpty())
	{
		if (const FSWGDataTableData* Table = GetRadialTable())
		{
			Text = MakeStandardLabelId(Table->GetCell(Entry.RadialId, TEXT("caption")));
		}
	}
	return FText::FromString(Tre ? Tre->ResolveStringId(Text) : Text);
}

const FSWGDataTableData* USWGRadialMenuSubsystem::GetRadialTable() const
{
	if (bTriedRadialTable)
	{
		return RadialTable.Get();
	}
	bTriedRadialTable = true;

	if (!Tre)
	{
		return nullptr;
	}

	static const FString TablePath = TEXT("datatables/player/radial_menu.iff");
	const FSWGIffReader Reader = Tre->CreateIffReader(TablePath);
	TUniquePtr<FSWGDataTableData> Loaded = MakeUnique<FSWGDataTableData>();
	if (!Reader.IsValid() || !FSWGDataTableReader::ReadDataTable(Reader, *Loaded))
	{
		UE_LOG(LogSWGRadial, Warning, TEXT("failed to load %s — standard radial options will be unlabeled"), *TablePath);
		return nullptr;
	}

	RadialTable = MoveTemp(Loaded);
	return RadialTable.Get();
}

int32 USWGRadialMenuSubsystem::ResolveRadialId(const FString& Name) const
{
	const FSWGDataTableData* Table = GetRadialTable();
	const int32 RadialId = Table ? Table->FindRowIndex(TEXT("caption"), Name) : INDEX_NONE;
	if (RadialId == INDEX_NONE)
	{
		UE_LOG(LogSWGRadial, Warning, TEXT("ResolveRadialId: no row captioned '%s' in datatables/player/radial_menu.iff — that option won't be offered"), *Name);
	}
	return RadialId;
}
