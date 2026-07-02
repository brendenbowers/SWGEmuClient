// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/UGalaxyListEntryWidget.h"
#include "UI/UGalaxyListEntryData.h"

void UGalaxyListEntryWidget::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	const UGalaxyListEntryData* EntryData = Cast<UGalaxyListEntryData>(ListItemObject);
	if (!EntryData)
	{
		return;
	}

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
}

void UGalaxyListEntryWidget::NativeOnItemSelectionChanged(bool bIsSelected)
{
	IUserObjectListEntry::NativeOnItemSelectionChanged(bIsSelected);

	if (RowBackground)
	{
		// Retail SWG "Protean" theme: Table.SelectionColorBackground = #35CBD7
		RowBackground->SetBrushColor(bIsSelected
			? FLinearColor(0.035601f, 0.597202f, 0.679542f, 0.9f)
			: FLinearColor(0.f, 0.f, 0.f, 0.f));
	}
}
