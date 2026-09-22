#include "SWGDatapadWidget.h"
#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "SWGDatapadQuery.h"
#include "SWGInventoryRowWidget.h"
#include "SWGUISettings.h"
#include "GameFramework/PlayerController.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"

void USWGDatapadWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetTitle(FText::FromString(TEXT("Datapad")));
	Refresh();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer, this, &USWGDatapadWidget::Refresh, RefreshInterval, true);
	}
}

void USWGDatapadWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	Super::NativeDestruct();
}

FReply USWGDatapadWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == ToggleKey)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void USWGDatapadWidget::Refresh()
{
	if (!SWGDatapadQuery::Gather(GetGameInstance(), Contents))
	{
		return;
	}

	BuildRows();

	DatapadHeader->SetText(FText::FromString(FString::Printf(TEXT("Datapad (%d)"), Contents.Num())));

	OnDatapadUpdated();
}

void USWGDatapadWidget::BuildRows()
{
	ContentsPanel->ClearChildren();
	Rows.Reset();

	ContentsEmptyText->SetVisibility(Contents.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	TSubclassOf<USWGInventoryRowWidget> RowClass = USWGUISettings::Get().InventoryRowClass.LoadSynchronous();
	if (!RowClass)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGDatapadWidget: SWG UI settings have no InventoryRowClass"));
		return;
	}

	for (const FSWGInventoryEntry& Entry : Contents)
	{
		USWGInventoryRowWidget* Row = CreateWidget<USWGInventoryRowWidget>(this, RowClass);
		Row->SetRow(Entry.ObjectId, Entry.Label(), Entry.SlotNames);
		Row->SetSelected(Entry.ObjectId == SelectedObjectId);
		Row->OnPressed.BindUObject(this, &USWGDatapadWidget::HandleRowPressed);

		ContentsPanel->AddChild(Row);
		Rows.Add(Row);
	}
}

void USWGDatapadWidget::HandleRowPressed(int64 ObjectId, FKey Button, FVector2D ScreenPosition)
{
	SelectedObjectId = ObjectId;
	for (USWGInventoryRowWidget* Row : Rows)
	{
		if (Row)
		{
			Row->SetSelected(Row->GetObjectId() == ObjectId);
		}
	}

	if (Button != EKeys::RightMouseButton)
	{
		return;
	}

	// The radial wants viewport pixels; the pointer event's position is
	// desktop-absolute, so ask the controller instead.
	APlayerController* PlayerController = GetOwningPlayer();
	UGameInstance* GameInstance = GetGameInstance();
	USWGRadialMenuSubsystem* Radial = GameInstance ? GameInstance->GetSubsystem<USWGRadialMenuSubsystem>() : nullptr;
	float MouseX = 0.f, MouseY = 0.f;
	if (Radial && PlayerController && PlayerController->GetMousePosition(MouseX, MouseY))
	{
		Radial->RequestMenu(ObjectId, FVector2D(MouseX, MouseY));
	}
}
