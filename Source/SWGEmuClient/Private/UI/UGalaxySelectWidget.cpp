// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/UGalaxySelectWidget.h"
#include "UI/UGalaxyListEntryData.h"
#include "Subsystems/SWGClientFlowSubsystem.h"

void UGalaxySelectWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BackButton)
	{
		BackButton->OnClicked.AddDynamic(this, &UGalaxySelectWidget::OnBackClicked);
	}
	if (NextButton)
	{
		NextButton->OnClicked.AddDynamic(this, &UGalaxySelectWidget::OnNextClicked);
	}

	RefreshGalaxyList();
}

void UGalaxySelectWidget::RefreshGalaxyList()
{
	if (!GalaxyListView)
	{
		return;
	}

	USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>();
	if (!FlowSubsystem)
	{
		return;
	}

	GalaxyEntries.Reset();
	for (const FSWGGalaxyInfo& Galaxy : FlowSubsystem->GetGalaxies())
	{
		UGalaxyListEntryData* EntryData = NewObject<UGalaxyListEntryData>(this);
		EntryData->Galaxy = Galaxy;
		GalaxyEntries.Add(EntryData);
	}

	GalaxyListView->SetListItems(GalaxyEntries);

	if (StatusText)
	{
		StatusText->SetText(GalaxyEntries.Num() > 0
			? FText::FromString(TEXT("Select a galaxy"))
			: FText::FromString(TEXT("No galaxies available")));
	}
}

void UGalaxySelectWidget::OnBackClicked()
{
	if (USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>())
	{
		FlowSubsystem->CancelToLogin();
	}
}

void UGalaxySelectWidget::OnNextClicked()
{
	if (!GalaxyListView)
	{
		return;
	}

	const UGalaxyListEntryData* Selected = GalaxyListView->GetSelectedItem<UGalaxyListEntryData>();
	if (!Selected)
	{
		return;
	}

	if (USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>())
	{
		FlowSubsystem->SelectGalaxy(Selected->Galaxy.GalaxyID);
	}
}
