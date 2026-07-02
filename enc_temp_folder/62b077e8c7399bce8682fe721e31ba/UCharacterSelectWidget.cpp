// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/UCharacterSelectWidget.h"
#include "UI/UCharacterListEntryData.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

void UCharacterSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BackButton)
	{
		BackButton->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnBackClicked);
	}
	if (NextButton)
	{
		NextButton->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnNextClicked);
	}
	if (CharacterListView)
	{
		CharacterListView->BP_OnItemSelectionChanged.AddDynamic(this, &UCharacterSelectWidget::OnCharacterSelectionChanged);
	}

	RefreshCharacterList();
}

void UCharacterSelectWidget::RefreshCharacterList()
{
	if (!CharacterListView)
	{
		return;
	}

	USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>();
	if (!FlowSubsystem)
	{
		return;
	}

	const TArray<FSWGGalaxyInfo> Galaxies = FlowSubsystem->GetGalaxies();

	CharacterEntries.Reset();
	for (const FSWGCharacterInfo& Character : FlowSubsystem->GetCharacters())
	{
		UCharacterListEntryData* EntryData = NewObject<UCharacterListEntryData>(this);
		EntryData->Character = Character;

		if (const FSWGGalaxyInfo* Galaxy = Galaxies.FindByPredicate([&Character](const FSWGGalaxyInfo& G)
			{
				return G.GalaxyID == Character.GalaxyID;
			}))
		{
			EntryData->GalaxyName = Galaxy->Name;
		}

		CharacterEntries.Add(EntryData);
	}

	CharacterListView->SetListItems(CharacterEntries);

	if (StatusText)
	{
		StatusText->SetText(CharacterEntries.Num() > 0
			? FText::FromString(TEXT("Select a character"))
			: FText::FromString(TEXT("No characters available")));
	}
	if (CharacterNamePreviewText)
	{
		CharacterNamePreviewText->SetText(FText::GetEmpty());
	}
}

void UCharacterSelectWidget::OnBackClicked()
{
	if (USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>())
	{
		FlowSubsystem->CancelToLogin();
	}
}

void UCharacterSelectWidget::OnNextClicked()
{
	if (!CharacterListView)
	{
		return;
	}

	const UCharacterListEntryData* Selected = CharacterListView->GetSelectedItem<UCharacterListEntryData>();
	if (!Selected)
	{
		return;
	}

	if (USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>())
	{
		FlowSubsystem->SelectCharacter(Selected->Character.CharacterID);
	}
}

void UCharacterSelectWidget::OnCharacterSelectionChanged(UObject* Item, bool bIsSelected)
{
	if (!CharacterNamePreviewText)
	{
		return;
	}

	if (!bIsSelected)
	{
		return;
	}

	const UCharacterListEntryData* EntryData = Cast<UCharacterListEntryData>(Item);
	CharacterNamePreviewText->SetText(EntryData ? FText::FromString(EntryData->Character.Name) : FText::GetEmpty());
}
