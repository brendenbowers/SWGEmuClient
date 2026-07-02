// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/UCharacterSelectWidget.h"
#include "UI/UCharacterListEntryData.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

void UCharacterSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ExitButton)
	{
		ExitButton->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnExitClicked);
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
}

void UCharacterSelectWidget::OnExitClicked()
{
	if (USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>())
	{
		FlowSubsystem->CancelToLogin();
	}
}
