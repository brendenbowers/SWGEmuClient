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
		RowBackground->SetBrushColor(bIsSelected
			? FLinearColor(0.10f, 0.55f, 0.65f, 0.9f)
			: FLinearColor(0.f, 0.f, 0.f, 0.f));
	}
}
