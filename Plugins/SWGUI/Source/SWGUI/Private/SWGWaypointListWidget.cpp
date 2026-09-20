#include "SWGWaypointListWidget.h"
#include "Subsystems/SWGWaypointSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"

void USWGWaypointListWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetTitle(NSLOCTEXT("SWGWaypoint", "ListTitle", "Waypoints"));

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USWGWaypointSubsystem* Waypoints = GameInstance->GetSubsystem<USWGWaypointSubsystem>())
		{
			Waypoints->OnWaypointListChanged.AddUniqueDynamic(this, &USWGWaypointListWidget::HandleWaypointListChanged);
		}
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer, this, &USWGWaypointListWidget::RefreshList, RefreshInterval, true);
	}

	RefreshList();
}

void USWGWaypointListWidget::NativeDestruct()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USWGWaypointSubsystem* Waypoints = GameInstance->GetSubsystem<USWGWaypointSubsystem>())
		{
			Waypoints->OnWaypointListChanged.RemoveDynamic(this, &USWGWaypointListWidget::HandleWaypointListChanged);
		}
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}

	Super::NativeDestruct();
}

void USWGWaypointListWidget::HandleWaypointListChanged()
{
	RefreshList();
}

void USWGWaypointListWidget::RefreshList()
{
	if (!WaypointListPanel)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	USWGWaypointSubsystem* Waypoints = GameInstance ? GameInstance->GetSubsystem<USWGWaypointSubsystem>() : nullptr;
	if (!Waypoints)
	{
		return;
	}

	TArray<FSWGWaypointEntry> Entries = Waypoints->GetWaypoints();
	Entries.Sort([](const FSWGWaypointEntry& A, const FSWGWaypointEntry& B) { return A.DistanceMeters < B.DistanceMeters; });

	WaypointListPanel->ClearChildren();

	for (const FSWGWaypointEntry& Entry : Entries)
	{
		const FLinearColor RowColor = Entry.bActive ? USWGWaypointSubsystem::GetWaypointColor(Entry.Color) : InactiveTextColor;

		UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>();

		auto AddColumn = [this, RowBox](const FText& Text, float FixedWidth, const FLinearColor& Color)
		{
			UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
			Block->SetText(Text);
			Block->SetColorAndOpacity(FSlateColor(Color));
			if (RowFont.HasValidFont())
			{
				Block->SetFont(RowFont);
			}
			Block->SetClipping(EWidgetClipping::ClipToBounds);

			UWidget* Cell = Block;
			if (FixedWidth > 0.f)
			{
				USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
				Box->SetWidthOverride(FixedWidth);
				Box->AddChild(Block);
				Cell = Box;
			}

			if (UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(RowBox->AddChild(Cell)))
			{
				Slot->SetSize(FSlateChildSize(FixedWidth > 0.f ? ESlateSizeRule::Automatic : ESlateSizeRule::Fill));
				Slot->SetPadding(FMargin(6.f, 2.f));
			}
		};

		// A small colour swatch stands in for the retail waypoint diamond icon.
		UBorder* Swatch = WidgetTree->ConstructWidget<UBorder>();
		Swatch->SetBrushColor(RowColor);
		USizeBox* SwatchBox = WidgetTree->ConstructWidget<USizeBox>();
		SwatchBox->SetWidthOverride(12.f);
		SwatchBox->SetHeightOverride(12.f);
		SwatchBox->AddChild(Swatch);
		if (UHorizontalBoxSlot* SwatchSlot = Cast<UHorizontalBoxSlot>(RowBox->AddChild(SwatchBox)))
		{
			SwatchSlot->SetPadding(FMargin(6.f, 2.f));
			SwatchSlot->SetVerticalAlignment(VAlign_Center);
		}

		AddColumn(Entry.Name, 0.f, RowColor);
		AddColumn(FText::FromString(Entry.Direction), 50.f, RowColor);
		AddColumn(Entry.bHasDistance ? FText::Format(NSLOCTEXT("SWGWaypoint", "DistanceMeters", "{0}m"), FText::AsNumber(FMath::RoundToInt(Entry.DistanceMeters))) : FText::GetEmpty(), 80.f, RowColor);
		AddColumn(Entry.bActive ? NSLOCTEXT("SWGWaypoint", "Active", "Active") : NSLOCTEXT("SWGWaypoint", "Inactive", ""), 70.f, RowColor);

		WaypointListPanel->AddChild(RowBox);
	}
}
