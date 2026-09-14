// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/UCharacterListEntryWidget.h"
#include "UI/UCharacterListEntryData.h"

void UCharacterListEntryWidget::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	const UCharacterListEntryData* EntryData = Cast<UCharacterListEntryData>(ListItemObject);
	if (!EntryData)
	{
		return;
	}

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

void UCharacterListEntryWidget::NativeOnItemSelectionChanged(bool bIsSelected)
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
