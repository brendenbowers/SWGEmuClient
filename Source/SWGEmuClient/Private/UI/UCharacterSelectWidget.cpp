// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/UCharacterSelectWidget.h"
#include "UI/UCharacterListEntryData.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/SlateBlueprintLibrary.h"

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
		CharacterListView->OnItemSelectionChanged().AddUObject(this, &UCharacterSelectWidget::OnCharacterSelectionChanged);
	}

	RefreshCharacterList();
}

void UCharacterSelectWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bPositionedPreviewSpawn)
	{
		PositionCharacterPreviewSpawnPoint();
	}
}

bool UCharacterSelectWidget::GetCharacterPreviewWorldLocation(float GroundZ, FVector2D PanelAnchor, FVector& OutLocation) const
{
	if (!CharacterPreviewPanel)
	{
		return false;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return false;
	}

	const FGeometry& PanelGeometry = CharacterPreviewPanel->GetCachedGeometry();
	if (PanelGeometry.GetLocalSize().IsNearlyZero())
	{
		// Not laid out yet - GetCachedGeometry() is only valid after the widget has painted once.
		return false;
	}

	const FVector2D PanelAnchorAbsolute = PanelGeometry.LocalToAbsolute(PanelGeometry.GetLocalSize() * PanelAnchor);

	FVector2D PixelPos, ViewportPos;
	USlateBlueprintLibrary::AbsoluteToViewport(this, PanelAnchorAbsolute, PixelPos, ViewportPos);

	FVector WorldPosition, WorldDirection;
	if (!UGameplayStatics::DeprojectScreenToWorld(PC, PixelPos, WorldPosition, WorldDirection))
	{
		return false;
	}

	// Intersect the ray with the horizontal ground plane at GroundZ
	const bool bRayHitsGround = !FMath::IsNearlyZero(WorldDirection.Z)
		&& ((GroundZ - WorldPosition.Z) / WorldDirection.Z) > 0.f;

	const float Distance = bRayHitsGround
		? (GroundZ - WorldPosition.Z) / WorldDirection.Z
		: CharacterPreviewFallbackDistance;

	OutLocation = WorldPosition + WorldDirection * Distance;
	return true;
}

bool UCharacterSelectWidget::PositionCharacterPreviewSpawnPoint()
{
	FVector WorldLocation;
	if (!GetCharacterPreviewWorldLocation(CharacterPreviewGroundZ, CharacterPreviewFeetAnchor, WorldLocation))
	{
		return false;
	}

	TArray<AActor*> SpawnPoints;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), CharacterPreviewSpawnPointTag, SpawnPoints);
	if (SpawnPoints.Num() > 0)
	{
		SpawnPoints[0]->SetActorLocation(WorldLocation);
	}

	UE_LOG(LogTemp, Warning, TEXT("UCharacterSelectWidget::PositionCharacterPreviewSpawnPoint on instance %s -> %s (found %d spawn point(s))"),
		*GetName(), *WorldLocation.ToString(), SpawnPoints.Num());

	bPositionedPreviewSpawn = true;
	return true;
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

void UCharacterSelectWidget::OnCharacterSelectionChanged(UObject* Item)
{
	const UCharacterListEntryData* EntryData = Cast<UCharacterListEntryData>(Item);
	if (CharacterNamePreviewText)
	{
		CharacterNamePreviewText->SetText(EntryData ? FText::FromString(EntryData->Character.Name) : FText::GetEmpty());
	}

	if (USWGClientFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<USWGClientFlowSubsystem>(); FlowSubsystem && EntryData)
	{
		FlowSubsystem->Context.SelectedCharacterID = EntryData->Character.CharacterID;
	}
}
