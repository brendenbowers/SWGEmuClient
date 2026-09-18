#include "SWGInventoryWidget.h"
#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "SWGInventoryQuery.h"
#include "SWGInventoryRowWidget.h"
#include "SWGUISettings.h"
#include "GameFramework/PlayerController.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"

void USWGInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetTitle(FText::FromString(TEXT("Inventory")));
	Refresh();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer, this, &USWGInventoryWidget::Refresh, RefreshInterval, true);
	}
}

void USWGInventoryWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	Super::NativeDestruct();
}

FReply USWGInventoryWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == ToggleKey)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void USWGInventoryWidget::Refresh()
{
	if (!SWGInventoryQuery::Gather(GetGameInstance(), Equipped, Contents))
	{
		return;
	}

	BuildRows(EquippedPanel, EquippedEmptyText, Equipped);
	BuildRows(InventoryPanel, InventoryEmptyText, Contents);

	InventoryHeader->SetText(FText::FromString(FString::Printf(TEXT("Inventory (%d)"), Contents.Num())));

	OnInventoryUpdated();
}

void USWGInventoryWidget::BuildRows(UPanelWidget* Panel, UWidget* EmptyLabel, const TArray<FSWGInventoryEntry>& Entries)
{
	Panel->ClearChildren();
	// ClearChildren orphaned this panel's rows; the other panel's keep their parent.
	Rows.RemoveAll([](const TObjectPtr<USWGInventoryRowWidget>& Row) { return !Row || Row->GetParent() == nullptr; });

	EmptyLabel->SetVisibility(Entries.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	TSubclassOf<USWGInventoryRowWidget> RowClass = USWGUISettings::Get().InventoryRowClass.LoadSynchronous();
	if (!RowClass)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGInventoryWidget: SWG UI settings have no InventoryRowClass"));
		return;
	}

	for (const FSWGInventoryEntry& Entry : Entries)
	{
		USWGInventoryRowWidget* Row = CreateWidget<USWGInventoryRowWidget>(this, RowClass);
		Row->SetRow(Entry.ObjectId, Entry.Label(), Entry.SlotNames);
		Row->SetSelected(Entry.ObjectId == SelectedObjectId);
		Row->OnPressed.BindUObject(this, &USWGInventoryWidget::HandleRowPressed);

		Panel->AddChild(Row);
		Rows.Add(Row);
	}
}

void USWGInventoryWidget::HandleRowPressed(int64 ObjectId, FKey Button, FVector2D ScreenPosition)
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
