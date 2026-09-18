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
#include "Objects/Creature/SWGCreature.h"
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
	// datatables/player/radial_menu.iff rows, by index (== Core3 RadialOptions).
	constexpr int32 RadialCombatAttack = 3;
	constexpr int32 RadialExamine = 7;
	constexpr int32 RadialItemEquip = 11;
	constexpr int32 RadialItemUnequip = 12;
	constexpr int32 RadialItemUse = 20;

	/** Retail names the standard options as @ui_radial:<row caption, lowercased>. */
	FString MakeStandardLabelId(const FString& Caption)
	{
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

	// Retail's client offers "Use" on usable tangibles and the server relies on
	// it: terminals act only on a select of ITEM_USE, and several menu
	// components nest their options under it (addRadialMenuItemToRadialID(20,
	// ...) throws when it's missing). So it goes in the request, comes back
	// flagged server-handled, and a pick sends ObjectMenuSelect. Attack and
	// Examine stay client-side (see AppendClientDefaults).
	const AActor* Actor = ObjectGraph->FindActor(ObjectId);
	if (Actor && (Actor->IsA<ASWGItem>() || Actor->IsA<ASWGInstallation>()))
	{
		FSWGRadialMenuEntry& Use = Request.ClientItems.AddDefaulted_GetRef();
		Use.Index = 1;
		Use.ParentIndex = 0;
		Use.RadialId = RadialItemUse;
		Use.Callback = 3;
	}

	Network->SendMessage(Request.Serialize());

	UE_LOG(LogSWGRadial, Verbose, TEXT("requested menu for %lld (counter %u)"), ObjectId, Request.Counter);
	return true;
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

	// Equip and Unequip are client commands in retail (the table's "equip" /
	// "unequip" never reach the server), and Use on a wearable means equip —
	// Core3's WearableObjectMenuComponent does nothing with ITEM_USE; the
	// retail client sends a transferItem* instead.
	USWGItemTransferSubsystem* Transfer = GetGameInstance()->GetSubsystem<USWGItemTransferSubsystem>();
	if (Transfer && (RadialId == RadialItemEquip || RadialId == RadialItemUnequip || RadialId == RadialItemUse) && Transfer->IsEquippable(ObjectId))
	{
		if (Transfer->IsEquipped(ObjectId))
		{
			Transfer->UnequipItem(ObjectId);
		}
		else
		{
			Transfer->EquipItem(ObjectId);
		}
		return;
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

	if (RadialId == RadialExamine)
	{
		if (USWGExamineSubsystem* Examine = GetGameInstance()->GetSubsystem<USWGExamineSubsystem>())
		{
			Examine->RequestExamine(ObjectId);
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

	auto AddDefault = [&](int32 RadialId)
	{
		if (AlreadyOffered(RadialId))
		{
			return;
		}
		FSWGRadialMenuItem& Item = Items.AddDefaulted_GetRef();
		Item.Index = ++NextIndex;
		Item.ParentIndex = 0;
		Item.RadialId = RadialId;
		const FString Caption = Table->GetCell(RadialId, TEXT("caption"));
		Item.Label = FText::FromString(Tre ? Tre->ResolveStringId(MakeStandardLabelId(Caption)) : Caption);
		Item.bServerHandled = false;
	};

	const bool bIsSelf = ObjectGraph && ObjectGraph->GetLocalPlayerObjectId() == ObjectId;
	const ASWGCreature* Creature = ObjectGraph ? Cast<ASWGCreature>(ObjectGraph->FindActor(ObjectId)) : nullptr;
	if (Creature && !bIsSelf)
	{
		AddDefault(RadialCombatAttack);
	}
	// Wearables and weapons: Equip / Unequip are the client's to offer (see SelectOption).
	if (const USWGItemTransferSubsystem* Transfer = GetGameInstance()->GetSubsystem<USWGItemTransferSubsystem>(); Transfer && Transfer->IsEquippable(ObjectId))
	{
		AddDefault(Transfer->IsEquipped(ObjectId) ? RadialItemUnequip : RadialItemEquip);
	}
	AddDefault(RadialExamine);
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
