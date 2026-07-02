// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/UCharacterListEntryWidget.h"
#include "UI/UCharacterListEntryData.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

void UCharacterListEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (SelectButton)
	{
		SelectButton->OnClicked.AddDynamic(this, &UCharacterListEntryWidget::OnSelectClicked);
	}
}

void UCharacterListEntryWidget::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	const UCharacterListEntryData* EntryData = Cast<UCharacterListEntryData>(ListItemObject);
	if (!EntryData)
	{
		return;
	}

	CharacterID = EntryData->Character.CharacterID;

	if (CharacterNameText)
	{
		CharacterNameText->SetText(FText::FromString(EntryData->Character.Name));
	}
	if (GalaxyText)
	{
		GalaxyText->SetText(FText::FromString(EntryData->GalaxyName));
	}
	if (StatusText)
	{
		StatusText->SetText(EntryData->Character.bActive ? FText::FromString(TEXT("Active")) : FText::GetEmpty());
	}
}

void UCharacterListEntryWidget::OnSelectClicked()
{
	if (USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>())
	{
		FlowSubsystem->SelectCharacter(CharacterID);
	}
}
