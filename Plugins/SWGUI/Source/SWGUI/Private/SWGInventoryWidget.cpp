#include "SWGInventoryWidget.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "SWGInventoryRowWidget.h"
#include "SWGUISettings.h"
#include "GameFramework/PlayerController.h"
#include "Components/SWGTangibleComponent.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Objects/Tangible/SWGItem.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"
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
	if (!Gather())
	{
		return;
	}

	BuildRows(EquippedPanel, EquippedEmptyText, Equipped);
	BuildRows(InventoryPanel, InventoryEmptyText, Contents);

	InventoryHeader->SetText(FText::FromString(FString::Printf(TEXT("Inventory (%d)"), Contents.Num())));

	OnInventoryUpdated();
}

bool USWGInventoryWidget::Gather()
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	USWGMeshGeneratorSubsystem* MeshGenerator = GameInstance ? GameInstance->GetSubsystem<USWGMeshGeneratorSubsystem>() : nullptr;
	const int64 PlayerId = ObjectGraph ? ObjectGraph->GetLocalPlayerObjectId() : 0;

	TArray<FSWGInventoryEntry> NewEquipped;
	TArray<FSWGInventoryEntry> NewContents;

	if (PlayerId != 0)
	{
		for (const int64 ObjectId : ObjectGraph->FindContainedObjectIds(PlayerId))
		{
			const int32* ContainmentType = ObjectGraph->FindContainmentType(ObjectId);
			if (!ContainmentType || !SWGIsSlottedArrangement(*ContainmentType))
			{
				continue;
			}

			FSWGInventoryEntry Entry = DescribeObject(ObjectId);

			// The bags (inventory, datapad, bank, mission_bag) are "equipped"
			// on the wire too, but sit in slots that never show on the body —
			// those aren't gear.
			AActor* Actor = ObjectGraph->FindActor(ObjectId);
			ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(Actor);
			TArray<FString> SlotNames;
			if (NetObject && MeshGenerator && MeshGenerator->ResolveArrangementSlotNames(NetObject->GetObjectCrc(), *ContainmentType, SlotNames))
			{
				if (!MeshGenerator->IsAnySlotAppearanceRelated(SlotNames))
				{
					continue;
				}
				Entry.SlotNames = FString::Join(SlotNames, TEXT(", "));
			}

			NewEquipped.Add(MoveTemp(Entry));
		}

		if (const int64 BagId = FindInventoryBagId(PlayerId))
		{
			for (const int64 ObjectId : ObjectGraph->FindContainedObjectIds(BagId))
			{
				NewContents.Add(DescribeObject(ObjectId));
			}
		}
	}

	NewEquipped.Sort([](const FSWGInventoryEntry& Left, const FSWGInventoryEntry& Right) { return Left.SlotNames < Right.SlotNames; });
	NewContents.Sort([](const FSWGInventoryEntry& Left, const FSWGInventoryEntry& Right) { return Left.Name < Right.Name; });

	if (NewEquipped == Equipped && NewContents == Contents)
	{
		return false;
	}

	Equipped = MoveTemp(NewEquipped);
	Contents = MoveTemp(NewContents);
	return true;
}

int64 USWGInventoryWidget::FindInventoryBagId(int64 PlayerId) const
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	USWGTreSubsystem* Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	if (!ObjectGraph || !Tre)
	{
		return 0;
	}

	for (const int64 ObjectId : ObjectGraph->FindContainedObjectIds(PlayerId))
	{
		if (ISWGNetworkObjectInterface* NetObject = Cast<ISWGNetworkObjectInterface>(ObjectGraph->FindActor(ObjectId)))
		{
			if (Tre->ResolveTemplatePath(NetObject->GetObjectCrc()).Contains(TEXT("character_inventory")))
			{
				return ObjectId;
			}
		}
	}
	return 0;
}

FSWGInventoryEntry USWGInventoryWidget::DescribeObject(int64 ObjectId) const
{
	UGameInstance* GameInstance = GetGameInstance();
	USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	AActor* Actor = ObjectGraph ? ObjectGraph->FindActor(ObjectId) : nullptr;

	FSWGInventoryEntry Entry;
	Entry.ObjectId = ObjectId;

	if (const USWGTangibleComponent* Tangible = Actor ? Actor->FindComponentByClass<USWGTangibleComponent>() : nullptr)
	{
		Entry.Name = Tangible->GetDisplayName();
	}

	if (const ASWGItem* Item = Cast<ASWGItem>(Actor); Item && Item->ResourceQuantity > 0)
	{
		Entry.Quantity = Item->ResourceQuantity;
		if (!Item->ResourceName.IsEmpty())
		{
			Entry.Name = Item->ResourceName;
		}
	}

	if (Entry.Name.IsEmpty())
	{
		// Baselines still in flight, or a template with no name at all.
		Entry.Name = FString::Printf(TEXT("object %lld"), ObjectId);
	}
	return Entry;
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
		const FString Label = Entry.Quantity > 0 ? FString::Printf(TEXT("%s x%d"), *Entry.Name, Entry.Quantity) : Entry.Name;
		Row->SetRow(Entry.ObjectId, Label, Entry.SlotNames);
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
