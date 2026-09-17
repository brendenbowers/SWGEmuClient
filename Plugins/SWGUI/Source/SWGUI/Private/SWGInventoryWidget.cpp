#include "SWGInventoryWidget.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGMeshGeneratorSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "SWGInventoryRowWidget.h"
#include "GameFramework/PlayerController.h"
#include "Components/SWGTangibleComponent.h"
#include "Objects/SWGNetworkObjectInterface.h"
#include "Objects/Tangible/SWGItem.h"
#include "Network/Objects/Zone/Object/SWGContainmentType.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"

namespace
{
	UTextBlock* MakeText(UWidgetTree* Tree, const FString& Text, int32 Size, FLinearColor Color)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
		Block->SetText(FText::FromString(Text));
		Block->SetColorAndOpacity(FSlateColor(Color));
		FSlateFontInfo Font = Block->GetFont();
		Font.Size = Size;
		Block->SetFont(Font);
		return Block;
	}
}

TSharedRef<SWidget> USWGInventoryWidget::RebuildWidget()
{
	// Only when no Blueprint tree exists — a subclass with its own layout
	// binds the named widgets instead.
	if (!WidgetTree->RootWidget)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
		WidgetTree->RootWidget = Root;

		USizeBox* Frame = WidgetTree->ConstructWidget<USizeBox>();
		Frame->SetWidthOverride(420.f);
		Frame->SetHeightOverride(560.f);
		UOverlaySlot* FrameSlot = Root->AddChildToOverlay(Frame);
		FrameSlot->SetHorizontalAlignment(HAlign_Right);
		FrameSlot->SetVerticalAlignment(VAlign_Center);
		FrameSlot->SetPadding(FMargin(0.f, 0.f, 40.f, 0.f));

		UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
		Background->SetBrushColor(FLinearColor(0.02f, 0.05f, 0.08f, 0.92f));
		Background->SetPadding(FMargin(12.f));
		Frame->AddChild(Background);

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		Background->AddChild(Column);

		TitleText = MakeText(WidgetTree, TEXT("Inventory"), 18, FLinearColor::White);
		Column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

		Column->AddChildToVerticalBox(MakeText(WidgetTree, TEXT("Equipped"), RowFontSize, FLinearColor::White))
			->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		UScrollBox* EquippedScroll = WidgetTree->ConstructWidget<UScrollBox>();
		EquippedPanel = EquippedScroll;
		UVerticalBoxSlot* EquippedSlot = Column->AddChildToVerticalBox(EquippedScroll);
		EquippedSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		EquippedSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

		InventoryHeader = MakeText(WidgetTree, TEXT("Inventory"), RowFontSize, FLinearColor::White);
		Column->AddChildToVerticalBox(InventoryHeader)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		UScrollBox* InventoryScroll = WidgetTree->ConstructWidget<UScrollBox>();
		InventoryPanel = InventoryScroll;
		UVerticalBoxSlot* InventorySlot = Column->AddChildToVerticalBox(InventoryScroll);
		FSlateChildSize InventorySize(ESlateSizeRule::Fill);
		InventorySize.Value = 1.5f;
		InventorySlot->SetSize(InventorySize);
	}
	return Super::RebuildWidget();
}

void USWGInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
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

UWidget* USWGInventoryWidget::NativeGetDesiredFocusTarget() const
{
	return const_cast<USWGInventoryWidget*>(this);
}

FReply USWGInventoryWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == ToggleKey || Key == EKeys::Gamepad_FaceButton_Right)
	{
		DeactivateWidget();
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

	BuildRows(EquippedPanel, Equipped, TEXT("Nothing equipped"));
	BuildRows(InventoryPanel, Contents, TEXT("Empty"));

	if (InventoryHeader)
	{
		InventoryHeader->SetText(FText::FromString(FString::Printf(TEXT("Inventory (%d)"), Contents.Num())));
	}

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

void USWGInventoryWidget::BuildRows(UPanelWidget* Panel, const TArray<FSWGInventoryEntry>& Entries, const FString& EmptyText)
{
	if (!Panel)
	{
		return;
	}
	Panel->ClearChildren();
	// ClearChildren orphaned this panel's rows; the other panel's keep their parent.
	Rows.RemoveAll([](const TObjectPtr<USWGInventoryRowWidget>& Row) { return !Row || Row->GetParent() == nullptr; });

	if (Entries.IsEmpty())
	{
		Panel->AddChild(MakeText(WidgetTree, EmptyText, RowFontSize, SlotTextColor));
		return;
	}

	for (const FSWGInventoryEntry& Entry : Entries)
	{
		USWGInventoryRowWidget* Row = CreateWidget<USWGInventoryRowWidget>(this, USWGInventoryRowWidget::StaticClass());
		if (!Row)
		{
			continue;
		}

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
