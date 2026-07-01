// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/UGalaxyListEntryWidget.h"
#include "UI/UGalaxyListEntryData.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

void UGalaxyListEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (SelectButton)
	{
		SelectButton->OnClicked.AddDynamic(this, &UGalaxyListEntryWidget::OnSelectClicked);
	}
}

void UGalaxyListEntryWidget::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	const UGalaxyListEntryData* EntryData = Cast<UGalaxyListEntryData>(ListItemObject);
	if (!EntryData)
	{
		return;
	}

	GalaxyID = EntryData->Galaxy.GalaxyID;

	if (GalaxyNameText)
	{
		GalaxyNameText->SetText(FText::FromString(EntryData->Galaxy.Name));
	}
	if (PopulationText)
	{
		PopulationText->SetText(FText::AsNumber(EntryData->Galaxy.Population));
	}
	if (StatusText)
	{
		StatusText->SetText(EntryData->Galaxy.bOnline ? FText::FromString(TEXT("Online")) : FText::FromString(TEXT("Offline")));
	}
	if (SelectButton)
	{
		SelectButton->SetIsEnabled(EntryData->Galaxy.bOnline);
	}
}

void UGalaxyListEntryWidget::OnSelectClicked()
{
	if (USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>())
	{
		FlowSubsystem->SelectGalaxy(GalaxyID);
	}
}
