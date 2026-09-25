#include "SWGHoloCharacterSheetWidget.h"
#include "SWGHoloAttributeLineWidget.h"
#include "SWGHoloMeterLineWidget.h"
#include "SWGHoloStyle.h"
#include "Components/TextBlock.h"
#include "Components/SWGEncumbranceComponent.h"
#include "Components/SWGForceComponent.h"
#include "Components/SWGHealthComponent.h"
#include "Components/SWGPlayerProfileComponent.h"
#include "Components/SWGStomachComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Objects/Creature/SWGCreature.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	constexpr int32 AttributeCount = 9;
	const TCHAR* const AttributeKeys[AttributeCount] = {
		TEXT("health"), TEXT("strength"), TEXT("constitution"),
		TEXT("action"), TEXT("quickness"), TEXT("stamina"),
		TEXT("mind"), TEXT("focus"), TEXT("willpower") };

	/** TotalPlayedTime counts 30-second ticks (Core3 PlayerObject::getTotalPlayedTime). */
	constexpr int32 SecondsPerPlayedTick = 30;
}

TArray<USWGHoloMeterLineWidget*> USWGHoloCharacterSheetWidget::AttributeMeters() const
{
	return { HealthMeter, StrengthMeter, ConstitutionMeter, ActionMeter, QuicknessMeter, StaminaMeter, MindMeter, FocusMeter, WillpowerMeter };
}

FText USWGHoloCharacterSheetWidget::Localized(const TCHAR* Table, const TCHAR* Key, const FText& Fallback) const
{
	USWGTreSubsystem* Tre = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGTreSubsystem>() : nullptr;
	const FString Value = Tre ? Tre->LookupString(Table, Key) : FString();
	return Value.IsEmpty() ? Fallback : FText::FromString(Value);
}

void USWGHoloCharacterSheetWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Retail's own strings, over whatever the designer typed.
	auto SetHeading = [this](UTextBlock* Heading, const FText& Text)
	{
		Heading->SetText(Text);
		if (bApplyHoloStyle)
		{
			// The holo font is a system face, not an asset, so a Blueprint can't pick it.
			Heading->SetFont(SWGHoloStyle::Font(12));
			Heading->SetColorAndOpacity(FSlateColor(SWGHoloStyle::Text));
		}
	};
	SetHeading(AttributesHeading, Localized(TEXT("ui_charsheet"), TEXT("attributes"), NSLOCTEXT("SWGEmu", "CharSheetAttributes", "Character Attributes")));
	SetHeading(StomachHeading, NSLOCTEXT("SWGEmu", "CharSheetStomach", "Stomach"));
	SetHeading(PersonalHeading, Localized(TEXT("ui_charsheet"), TEXT("personal"), NSLOCTEXT("SWGEmu", "CharSheetPersonal", "Personal")));

	const TArray<USWGHoloMeterLineWidget*> Meters = AttributeMeters();
	for (int32 Index = 0; Index < AttributeCount; ++Index)
	{
		Meters[Index]->SetLabel(Localized(TEXT("att_n"), AttributeKeys[Index], FText::FromString(AttributeKeys[Index])));
	}
	ForceMeter->SetLabel(Localized(TEXT("ui_charsheet"), TEXT("force_power"), NSLOCTEXT("SWGEmu", "CharSheetForce", "Force Power")));
	FoodMeter->SetLabel(Localized(TEXT("ui_charsheet"), TEXT("food"), NSLOCTEXT("SWGEmu", "CharSheetFood", "Food")));
	DrinkMeter->SetLabel(Localized(TEXT("ui_charsheet"), TEXT("drink"), NSLOCTEXT("SWGEmu", "CharSheetDrink", "Drink")));

	FatigueLabel = Localized(TEXT("ui_charsheet"), TEXT("shock_wounds"), NSLOCTEXT("SWGEmu", "CharSheetFatigue", "Battle Fatigue"));
	SpeciesLabel = Localized(TEXT("ui_charsheet"), TEXT("specieslabel"), NSLOCTEXT("SWGEmu", "CharSheetSpecies", "Species"));
	TitleLabel = NSLOCTEXT("SWGEmu", "CharSheetTitle", "Title");
	PlayedLabel = Localized(TEXT("ui_charsheet"), TEXT("playedlabel"), NSLOCTEXT("SWGEmu", "CharSheetPlayed", "Time Played"));

	// The scroll box takes the wheel; clicks fall through to the hologram.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void USWGHoloCharacterSheetWidget::Refresh()
{
	const ASWGCreature* Creature = Cast<ASWGCreature>(GetOwningPlayerPawn());
	if (!Creature)
	{
		return;
	}
	const USWGHealthComponent* Health = Creature->HealthComponent;
	const USWGEncumbranceComponent* Encumbrance = Creature->EncumbranceComponent;
	auto At = [](const TArray<int32>& List, int32 Index) { return List.IsValidIndex(Index) ? List[Index] : 0; };
	const TArray<USWGHoloMeterLineWidget*> Meters = AttributeMeters();
	for (int32 Index = 0; Health && Index < AttributeCount; ++Index)
	{
		const int32 Max = At(Health->MaxHAM.Items, Index);
		const int32 Wounds = At(Health->Wounds.Items, Index);
		// Armour's encumbrance comes off the pool's two secondaries' max (PlayerManager::applyEncumbrancies).
		const bool bSecondary = Index % 3 != 0;
		const int32 Encumbered = bSecondary && Encumbrance ? At(Encumbrance->Encumbrances.Items, Index / 3) : 0;
		const int32 Modifier = Health->bHasBase1 ? Max - At(Health->BaseHAM.Items, Index) + Encumbered : 0;
		TArray<FString> Notes;
		if (Wounds > 0)
		{
			Notes.Add(FText::Format(NSLOCTEXT("SWGEmu", "CharSheetWounds", "Wounds {0}"), FText::AsNumber(Wounds)).ToString());
		}
		if (Modifier != 0)
		{
			Notes.Add(FText::Format(NSLOCTEXT("SWGEmu", "CharSheetModifier", "Modifier {0}{1}"), FText::FromString(Modifier > 0 ? TEXT("+") : TEXT("")), FText::AsNumber(Modifier)).ToString());
		}
		if (Encumbered > 0)
		{
			Notes.Add(FText::Format(NSLOCTEXT("SWGEmu", "CharSheetEncumbrance", "Encumbrance {0}"), FText::AsNumber(Encumbered)).ToString());
		}
		Meters[Index]->SetMeter(At(Health->HAM.Items, Index), Max, FText::FromString(FString::Join(Notes, TEXT("  •  "))));
	}
	FatigueLine->SetAttribute(FatigueLabel, FText::AsNumber(Health ? Health->ShockWounds : 0), false);

	const USWGForceComponent* Force = Creature->FindComponentByClass<USWGForceComponent>();
	const bool bForceSensitive = Force && Force->ForcePowerMax > 0;
	ForceMeter->SetVisibility(bForceSensitive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (bForceSensitive)
	{
		ForceMeter->SetMeter(Force->ForcePower, Force->ForcePowerMax, FText::GetEmpty());
	}
	if (const USWGStomachComponent* Stomach = Creature->FindComponentByClass<USWGStomachComponent>())
	{
		FoodMeter->SetMeter(Stomach->FoodFilling, Stomach->FoodFillingMax, FText::GetEmpty());
		DrinkMeter->SetMeter(Stomach->DrinkFilling, Stomach->DrinkFillingMax, FText::GetEmpty());
	}

	USWGTreSubsystem* Tre = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGTreSubsystem>() : nullptr;
	// The player template's objectName is @species:<name>.
	const FString Species = Tre ? Tre->ResolveTemplateObjectName(Creature->GetObjectCrc()) : FString();
	SpeciesLine->SetAttribute(SpeciesLabel, Species.IsEmpty() ? Localized(TEXT("ui_charsheet"), TEXT("unknown"), NSLOCTEXT("SWGEmu", "CharSheetUnknown", "Unknown")) : FText::FromString(Species), false);

	const USWGPlayerProfileComponent* Profile = Creature->FindComponentByClass<USWGPlayerProfileComponent>();
	// The title is a skill name; retail shows it through skl_t.
	const FString Title = Profile ? Profile->Title : FString();
	const FString TitleText = Title.IsEmpty() || !Tre ? Title : Tre->LookupString(TEXT("skl_t"), Title);
	TitleLine->SetVisibility(Title.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	TitleLine->SetAttribute(TitleLabel, FText::FromString(TitleText.IsEmpty() ? Title : TitleText), false);

	const int64 Minutes = Profile ? int64(Profile->TotalPlayedTime) * SecondsPerPlayedTick / 60 : 0;
	const int64 Days = Minutes / (60 * 24);
	const int64 Hours = Minutes / 60 % 24;
	PlayedLine->SetAttribute(PlayedLabel, Days > 0
		? FText::Format(NSLOCTEXT("SWGEmu", "CharSheetPlayedDays", "{0}d {1}h {2}m"), FText::AsNumber(Days), FText::AsNumber(Hours), FText::AsNumber(Minutes % 60))
		: FText::Format(NSLOCTEXT("SWGEmu", "CharSheetPlayedHours", "{0}h {1}m"), FText::AsNumber(Hours), FText::AsNumber(Minutes % 60)), false);
}
