#include "SWGActionBarWidget.h"
#include "Components/SWGSkillComponent.h"
#include "Subsystems/SWGCommandSubsystem.h"
#include "Subsystems/SWGCombatSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTargetSubsystem.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGUiSettingsReader.h"
#include "Misc/FileHelper.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/ScaleBox.h"

namespace
{
	USWGTreSubsystem* GetTre(const UWidget* Widget)
	{
		UGameInstance* GameInstance = Widget ? Widget->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	}

	USWGObjectGraphSubsystem* GetObjectGraph(const UWidget* Widget)
	{
		UGameInstance* GameInstance = Widget ? Widget->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	}

	USWGCommandSubsystem* GetCommands(const UWidget* Widget)
	{
		UGameInstance* GameInstance = Widget ? Widget->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USWGCommandSubsystem>() : nullptr;
	}

	USWGCombatSubsystem* GetCombat(const UWidget* Widget)
	{
		UGameInstance* GameInstance = Widget ? Widget->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USWGCombatSubsystem>() : nullptr;
	}

	USWGTargetSubsystem* GetTargeting(const UWidget* Widget)
	{
		UGameInstance* GameInstance = Widget ? Widget->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USWGTargetSubsystem>() : nullptr;
	}
}

void USWGActionBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BuildSlotWidgets();

	// The retail toolbar is what the player arranged themselves, so it wins
	// outright; the seed and ability fill are only for characters without one.
	if (!(bLoadRetailToolbar && LoadRetailToolbar()))
	{
		// Before the ability fill, so attack keeps slot 0 rather than being
		// pushed along by whatever the player happens to know.
		if (bSeedDefaultAttackSlot)
		{
			SeedDefaultAttackSlot();
		}

		if (bFillEmptySlotsFromAbilities)
		{
			FillEmptySlotsFromAbilities();
		}
	}

	RefreshSlotVisuals();
}

void USWGActionBarWidget::SeedDefaultAttackSlot()
{
	const USWGCombatSubsystem* Combat = GetCombat(this);
	const FString AttackCommand = Combat ? Combat->AttackCommandName : TEXT("attack");

	if (Slots.Num() < GetActiveSlotCount())
	{
		Slots.SetNum(GetActiveSlotCount());
	}

	if (Slots.IsEmpty() || !Slots[0].IsEmpty())
	{
		return;
	}

	Slots[0].CommandName = AttackCommand;
}

bool USWGActionBarWidget::LoadRetailToolbar()
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGClientFlowSubsystem* Flow = GameInstance ? GameInstance->GetSubsystem<USWGClientFlowSubsystem>() : nullptr;
	USWGTreSubsystem* Tre = GetTre(this);
	if (!Flow || !Tre || Flow->GetSelectedCharacterID() <= 0)
	{
		return false;
	}

	const FString Path = FSWGUiSettingsReader::MakeUiSettingsPath(
		Tre->GetTreDirectory(), Flow->GetUsername(), Flow->GetSelectedGalaxyName(), Flow->GetSelectedCharacterID());

	TArray<uint8> Data;
	TArray<FSWGToolbarSlot> RetailSlots;
	if (!FFileHelper::LoadFileToArray(Data, *Path) || !FSWGUiSettingsReader::ReadToolbar(Data, RetailSlots))
	{
		UE_LOG(LogTemp, Log, TEXT("USWGActionBarWidget: no retail toolbar at %s"), *Path);
		return false;
	}

	// Retail has six panes of 24; we show the first pane, and as many of its
	// slots as the layout has room for.
	Slots.Reset();
	Slots.SetNum(GetActiveSlotCount());

	int32 LoadedCount = 0;
	for (const FSWGToolbarSlot& Retail : RetailSlots)
	{
		if (Retail.Pane != 0 || !Slots.IsValidIndex(Retail.Slot) || !Retail.IsCommand())
		{
			continue;
		}

		// "/mood sad" -> mood + sad. Retail also stores the leading slash.
		FString Line = Retail.Text;
		Line.RemoveFromStart(TEXT("/"));
		FString Command, Arguments;
		if (!Line.Split(TEXT(" "), &Command, &Arguments))
		{
			Command = Line;
		}
		if (Command.IsEmpty())
		{
			continue;
		}

		Slots[Retail.Slot].CommandName = Command;
		Slots[Retail.Slot].Arguments = Arguments.TrimStartAndEnd();
		++LoadedCount;
	}

	UE_LOG(LogTemp, Log, TEXT("USWGActionBarWidget: loaded %d slots from retail toolbar %s"), LoadedCount, *Path);
	bRetailToolbarLoaded = true;
	return true;
}

int32 USWGActionBarWidget::FillEmptySlotsFromAbilities()
{
	const TArray<FString> Abilities = GetAvailableAbilities();
	if (Abilities.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("USWGActionBarWidget: no abilities to fill from — the player object's base9 list is empty"));
		return 0;
	}

	if (Slots.Num() < GetActiveSlotCount())
	{
		Slots.SetNum(GetActiveSlotCount());
	}

	int32 FilledCount = 0;
	int32 NextAbility = 0;

	for (FSWGActionSlot& SlotData : Slots)
	{
		if (!SlotData.IsEmpty())
		{
			continue;
		}

		// Skip anything already sitting in another slot.
		while (Abilities.IsValidIndex(NextAbility) &&
			Slots.ContainsByPredicate([&Abilities, NextAbility](const FSWGActionSlot& Existing)
			{
				return Existing.CommandName == Abilities[NextAbility];
			}))
		{
			++NextAbility;
		}

		if (!Abilities.IsValidIndex(NextAbility))
		{
			break;
		}

		SlotData.CommandName = Abilities[NextAbility++];
		++FilledCount;
	}

	UE_LOG(LogTemp, Log, TEXT("USWGActionBarWidget: filled %d slot(s) from %d available ability/abilities"), FilledCount, Abilities.Num());

	RefreshSlotVisuals();
	return FilledCount;
}

FText USWGActionBarWidget::GetSlotKeyLabel(int32 SlotIndex)
{
	// 1-9, then 0, then the two keys past it, matching SWG's numbering.
	static const TCHAR* Keys[] = { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"),
								   TEXT("7"), TEXT("8"), TEXT("9"), TEXT("0"), TEXT("-"), TEXT("=") };

	return Keys && SlotIndex >= 0 && SlotIndex < UE_ARRAY_COUNT(Keys)
		? FText::FromString(Keys[SlotIndex])
		: FText::GetEmpty();
}

FText USWGActionBarWidget::GetGamepadSlotKeyLabel(int32 SlotIndex)
{
	// D-pad clockwise from up, then the face buttons — the order
	// ASWGPlayer::SetupPlayerInputComponent binds them in.
	static const TCHAR* Keys[] = { TEXT("▲"), TEXT("▶"), TEXT("▼"), TEXT("◀"),
								   TEXT("A"), TEXT("B"), TEXT("X"), TEXT("Y") };

	return SlotIndex >= 0
		? FText::FromString(Keys[SlotIndex % UE_ARRAY_COUNT(Keys)])
		: FText::GetEmpty();
}

void USWGActionBarWidget::SetGamepadLayout(bool bGamepad)
{
	if (bGamepadLayout == bGamepad && !SlotWidgets.IsEmpty())
	{
		return;
	}

	bGamepadLayout = bGamepad;
	BuildSlotWidgets();

	// The gamepad layout has four more slots than the keyboard one, so there
	// may be empties to fill the first time it comes up.
	if (bRetailToolbarLoaded)
	{
		LoadRetailToolbar();
	}
	else if (bFillEmptySlotsFromAbilities)
	{
		FillEmptySlotsFromAbilities();
	}

	RefreshSlotVisuals();
}

void USWGActionBarWidget::SetActiveBank(int32 BankIndex)
{
	ActiveBank = BankIndex;
	ApplyBankHighlight();
}

void USWGActionBarWidget::ApplyBankHighlight()
{
	for (int32 GroupIndex = 0; GroupIndex < GroupWidgets.Num(); ++GroupIndex)
	{
		if (UPanelWidget* Group = GroupWidgets[GroupIndex])
		{
			Group->SetRenderOpacity(bGamepadLayout && GroupIndex != ActiveBank ? InactiveBankOpacity : 1.f);
		}
	}
}

void USWGActionBarWidget::BuildSlotWidgets()
{
	if (!SlotBox || !SlotWidgetClass)
	{
		// Silent here means an empty bar with nothing to explain it — the usual
		// cause is the widget instance overriding SlotWidgetClass with None.
		UE_LOG(LogTemp, Warning, TEXT("USWGActionBarWidget: no slots built — SlotBox %s, SlotWidgetClass %s"),
			SlotBox ? TEXT("bound") : TEXT("MISSING"),
			SlotWidgetClass ? TEXT("set") : TEXT("MISSING"));
		return;
	}

	SlotBox->ClearChildren();
	SlotWidgets.Reset();
	GroupWidgets.Reset();

	const FSlateBrush* FrameBrush = ResolveStyleBrush(SlotFrameStyle);
	if (bGamepadLayout)
	{
		BuildGamepadSlots(FrameBrush);
	}
	else
	{
		BuildKeyboardSlots(FrameBrush);
	}

	ApplyBankHighlight();
}

USWGActionSlotWidget* USWGActionBarWidget::MakeSlotWidget(int32 SlotIndex, const FSlateBrush* FrameBrush)
{
	USWGActionSlotWidget* SlotWidget = CreateWidget<USWGActionSlotWidget>(this, SlotWidgetClass);
	if (!SlotWidget)
	{
		return nullptr;
	}

	SlotWidget->InitialiseSlot(this, SlotIndex, bGamepadLayout ? GetGamepadSlotKeyLabel(SlotIndex) : GetSlotKeyLabel(SlotIndex));
	SlotWidget->SetFrame(FrameBrush);
	// Icon-only tiles keep the diamonds square; the keyboard row has room for names.
	SlotWidget->SetCompact(bGamepadLayout);
	SlotWidgets.Add(SlotWidget);
	return SlotWidget;
}

void USWGActionBarWidget::BuildKeyboardSlots(const FSlateBrush* FrameBrush)
{
	const int32 SlotsPerGroup = FMath::Max(1, GroupSize);
	UHorizontalBox* Group = nullptr;

	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		if (SlotIndex % SlotsPerGroup == 0)
		{
			Group = WidgetTree->ConstructWidget<UHorizontalBox>();
			if (UHorizontalBoxSlot* GroupSlot = Cast<UHorizontalBoxSlot>(SlotBox->AddChild(Group)); GroupSlot && SlotIndex > 0)
			{
				GroupSlot->SetPadding(FMargin(GroupSpacing, 0.f, 0.f, 0.f));
			}
			GroupWidgets.Add(Group);
		}

		if (USWGActionSlotWidget* SlotWidget = MakeSlotWidget(SlotIndex, FrameBrush))
		{
			Group->AddChildToHorizontalBox(SlotWidget);
		}
	}
}

void USWGActionBarWidget::BuildGamepadSlots(const FSlateBrush* FrameBrush)
{
	// Row/column in a 3x3 grid for each slot of a bank, laid out like the
	// pad: D-pad Up/Right/Down/Left then A/B/X/Y (A at the bottom, Y on top)
	// — the order ASWGPlayer binds them in.
	static const FIntPoint DiamondCells[GamepadBankSize] = {
		FIntPoint(0, 1), FIntPoint(1, 2), FIntPoint(2, 1), FIntPoint(1, 0),
		FIntPoint(2, 1), FIntPoint(1, 2), FIntPoint(1, 0), FIntPoint(0, 1)
	};
	constexpr int32 DiamondSize = 4;

	for (int32 BankIndex = 0; BankIndex < GamepadBankCount; ++BankIndex)
	{
		// The scale box shrinks the bank's layout size, not just its paint,
		// so the diamonds pack together and the bar stays on screen.
		UScaleBox* BankScale = WidgetTree->ConstructWidget<UScaleBox>();
		BankScale->SetStretch(EStretch::UserSpecified);
		BankScale->SetUserSpecifiedScale(GamepadSlotScale);
		if (UHorizontalBoxSlot* BankSlot = Cast<UHorizontalBoxSlot>(SlotBox->AddChild(BankScale)); BankSlot && BankIndex > 0)
		{
			BankSlot->SetPadding(FMargin(BankSpacing, 0.f, 0.f, 0.f));
		}

		UHorizontalBox* Bank = WidgetTree->ConstructWidget<UHorizontalBox>();
		BankScale->AddChild(Bank);
		GroupWidgets.Add(Bank);

		UGridPanel* Diamond = nullptr;
		for (int32 SlotInBank = 0; SlotInBank < GamepadBankSize; ++SlotInBank)
		{
			if (SlotInBank % DiamondSize == 0)
			{
				Diamond = WidgetTree->ConstructWidget<UGridPanel>();
				if (UHorizontalBoxSlot* DiamondSlot = Bank->AddChildToHorizontalBox(Diamond); DiamondSlot && SlotInBank > 0)
				{
					DiamondSlot->SetPadding(FMargin(DiamondSpacing, 0.f, 0.f, 0.f));
				}
			}

			if (USWGActionSlotWidget* SlotWidget = MakeSlotWidget(BankIndex * GamepadBankSize + SlotInBank, FrameBrush))
			{
				const FIntPoint& Cell = DiamondCells[SlotInBank];
				if (UGridSlot* CellSlot = Diamond->AddChildToGrid(SlotWidget, Cell.X, Cell.Y))
				{
					CellSlot->SetHorizontalAlignment(HAlign_Center);
					CellSlot->SetVerticalAlignment(VAlign_Center);

					// A gap all round, then negative padding on the side facing
					// the centre shrinks that row/column so the arm hangs into
					// the empty middle.
					FMargin Tuck(TileSpacing);
					if (Cell.X == 0) { Tuck.Bottom -= DiamondOverlap; }
					if (Cell.X == 2) { Tuck.Top -= DiamondOverlap; }
					if (Cell.Y == 0) { Tuck.Right -= DiamondOverlap; }
					if (Cell.Y == 2) { Tuck.Left -= DiamondOverlap; }
					CellSlot->SetPadding(Tuck);
				}
			}
		}
	}
}

void USWGActionBarWidget::RefreshSlotVisuals()
{
	for (int32 SlotIndex = 0; SlotIndex < SlotWidgets.Num(); ++SlotIndex)
	{
		USWGActionSlotWidget* SlotWidget = SlotWidgets[SlotIndex];
		if (!SlotWidget)
		{
			continue;
		}

		if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
		{
			SlotWidget->SetCommandLabel(FText::GetEmpty());
			SlotWidget->SetCommandIcon(nullptr);
			continue;
		}

		const FSWGActionSlot& SlotData = Slots[SlotIndex];
		FText Label = SlotData.Label.IsEmpty() ? ResolveCommandName(SlotData.CommandName) : SlotData.Label;
		if (SlotData.Label.IsEmpty() && !SlotData.Arguments.IsEmpty())
		{
			// "Mood sad", so two mood slots don't read the same.
			Label = FText::FromString(Label.ToString() + TEXT(" ") + SlotData.Arguments);
		}
		const FSlateBrush* Icon = ResolveCommandIcon(SlotData.CommandName);
		SlotWidget->SetCommandLabel(Label);
		SlotWidget->SetCommandIcon(Icon);

		UE_LOG(LogTemp, Verbose, TEXT("USWGActionBarWidget: slot %d '%s' -> '%s', icon %s"),
			SlotIndex, *SlotData.CommandName, *Label.ToString(), Icon ? TEXT("yes") : TEXT("none"));
	}
}

FText USWGActionBarWidget::ResolveCommandName(const FString& CommandName) const
{
	USWGTreSubsystem* Tre = GetTre(this);
	if (!Tre)
	{
		return FText::FromString(CommandName);
	}

	FString Name = CommandName.ToLower();
	FString Display = Tre->LookupString(TEXT("cmd_n"), Name);
	if (Display.IsEmpty() && Name.RemoveFromEnd(TEXT("server")))
	{
		// Server-side commands the client wraps ("sitserver") are named after the client command.
		Display = Tre->LookupString(TEXT("cmd_n"), Name);
	}
	return FText::FromString(Display.IsEmpty() ? CommandName : Display);
}

namespace
{
	const FSlateBrush* MakeStyleBrush(USWGTreSubsystem* Tre, const FSWGUIImageStyle* Style, const FString& CacheKey, TMap<FString, FSlateBrush>& Cache)
	{
		UTexture2D* Sheet = (Tre && Style) ? Tre->GetOrLoadTexture(FString::Printf(TEXT("texture/%s.dds"), *Style->Source)) : nullptr;
		if (!Sheet || Sheet->GetSizeX() <= 0 || Sheet->GetSizeY() <= 0)
		{
			return nullptr;
		}

		// The style's rect is in sheet pixels; the brush wants it as a UV box.
		const FVector2D SheetSize(Sheet->GetSizeX(), Sheet->GetSizeY());
		const FBox2D UVRegion(FVector2D(Style->SourceRect.Min) / SheetSize, FVector2D(Style->SourceRect.Max) / SheetSize);

		FSlateBrush Brush;
		Brush.SetResourceObject(Sheet);
		Brush.ImageSize = FVector2D(Style->SourceRect.Size());
		Brush.SetUVRegion(UVRegion);
		return &Cache.Add(CacheKey, MoveTemp(Brush));
	}
}

const FSlateBrush* USWGActionBarWidget::ResolveCommandIcon(const FString& CommandName)
{
	if (CommandName.IsEmpty())
	{
		return nullptr;
	}

	// Prefixed so a command can't collide with a style path in the shared cache.
	const FString Key = TEXT("command:") + CommandName.ToLower();
	if (const FSlateBrush* Cached = StyleBrushes.Find(Key))
	{
		return Cached;
	}

	USWGTreSubsystem* Tre = GetTre(this);
	return MakeStyleBrush(Tre, Tre ? Tre->FindCommandIcon(CommandName) : nullptr, Key, StyleBrushes);
}

const FSlateBrush* USWGActionBarWidget::ResolveStyleBrush(const FString& DottedPath)
{
	if (DottedPath.IsEmpty())
	{
		return nullptr;
	}

	const FString Key = DottedPath.ToLower();
	if (const FSlateBrush* Cached = StyleBrushes.Find(Key))
	{
		return Cached;
	}

	USWGTreSubsystem* Tre = GetTre(this);
	const FSWGUIStyleSheet* Sheet = Tre ? Tre->GetUIStyleSheet() : nullptr;
	return MakeStyleBrush(Tre, Sheet ? Sheet->FindImageStyle(Key) : nullptr, Key, StyleBrushes);
}

int64 USWGActionBarWidget::ResolveTargetId() const
{
	// Not the CREO6 TargetId: the server never echoes our own selection into
	// that field, so it goes stale and an attack would fire at (and retarget
	// to) whatever the server last remembered.
	const USWGTargetSubsystem* Targeting = GetTargeting(this);
	return Targeting ? Targeting->GetTargetId() : 0;
}

bool USWGActionBarWidget::TriggerSlot(int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
	{
		return false;
	}

	if (!SendCommand(Slots[SlotIndex].CommandName, Slots[SlotIndex].Arguments))
	{
		return false;
	}

	OnSlotTriggered(SlotIndex);
	return true;
}

bool USWGActionBarWidget::SendCommand(const FString& CommandName, const FString& Arguments)
{
	// The basic attack is the one command with client-side repeat semantics —
	// the server runs a queued command once and drains its queue, so sustained
	// combat is the client re-sending. Route it through the combat subsystem,
	// which owns that loop, and let the press toggle it the way SWG's toolbar
	// does. Everything else is a plain one-shot.
	USWGCombatSubsystem* Combat = GetCombat(this);
	if (Combat && Arguments.IsEmpty() && CommandName.Equals(Combat->AttackCommandName, ESearchCase::IgnoreCase))
	{
		// True means "the press did something", not "an attack started" —
		// stopping one is just as much a handled press, and the slot should
		// still flash for it.
		const bool bWasAttacking = Combat->IsAttacking();
		return Combat->ToggleAttack(ResolveTargetId()) || bWasAttacking;
	}

	USWGCommandSubsystem* Commands = GetCommands(this);
	return Commands && Commands->SendCommand(CommandName, ResolveTargetId(), Arguments) != 0;
}

void USWGActionBarWidget::SetSlotCommand(int32 SlotIndex, const FString& CommandName, FText Label)
{
	if (SlotIndex < 0)
	{
		return;
	}

	if (!Slots.IsValidIndex(SlotIndex))
	{
		Slots.SetNum(SlotIndex + 1);
	}

	Slots[SlotIndex].CommandName = CommandName;
	Slots[SlotIndex].Label = Label;

	RefreshSlotVisuals();
}

TArray<FString> USWGActionBarWidget::GetAvailableAbilities() const
{
	USWGObjectGraphSubsystem* ObjectGraph = GetObjectGraph(this);
	if (!ObjectGraph)
	{
		return {};
	}

	const USWGSkillComponent* Skills =
		ObjectGraph->FindComponent<USWGSkillComponent>(ObjectGraph->GetLocalPlayerObjectId());

	if (!Skills)
	{
		return {};
	}

	// The ability list also carries skill markers like "private_brawler_novice",
	// which aren't commands — a slot holding one is a button that does nothing.
	const USWGCommandSubsystem* Commands = GetCommands(this);
	if (!Commands)
	{
		return Skills->AbilityList.Items;
	}

	return Skills->AbilityList.Items.FilterByPredicate(
		[Commands](const FString& Ability) { return Commands->IsKnownCommand(Ability); });
}
