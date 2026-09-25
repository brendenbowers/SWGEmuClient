#include "SWGHoloInventoryWidget.h"
#include "SWGHoloFigureActor.h"
#include "SWGHoloDetailCardWidget.h"
#include "SWGHoloLabelWidget.h"
#include "SWGHoloShelfActor.h"
#include "SWGHoloStyle.h"
#include "SWGRetailStyle.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SWGEquipmentComponent.h"
#include "Subsystems/SWGRadialMenuSubsystem.h"
#include "Camera/CameraActor.h"
#include "Components/Border.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	constexpr float TurnDegreesPerPixel = 0.4f;
	constexpr float AnalogDeadZone = 0.2f;
	constexpr float AnalogTurnSpeed = 120.f;
	const FLinearColor HintColor = FLinearColor::FromSRGBColor(FColor(0x96, 0xF4, 0xFC));
	const FLinearColor LineUnderlay(0.f, 0.02f, 0.05f, 0.55f);
	/** How long a hovered item's details linger after the pointer leaves its name, so moving onto the card doesn't drop it. */
	constexpr double CardLingerSeconds = 0.25;

	UButton* MakeHintButton(UWidgetTree* Tree, UHorizontalBox* Row, const FText& Label)
	{
		UButton* Button = Tree->ConstructWidget<UButton>();
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
		Text->SetText(Label);
		Button->AddChild(Text);
		if (UHorizontalBoxSlot* ButtonSlot = Row->AddChildToHorizontalBox(Button))
		{
			ButtonSlot->SetPadding(FMargin(6.f, 0.f));
		}
		return Button;
	}
}

void USWGHoloInventoryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Root;
		UHorizontalBox* Bar = WidgetTree->ConstructWidget<UHorizontalBox>();
		Root->AddChild(Bar);
		if (UCanvasPanelSlot* BarSlot = Cast<UCanvasPanelSlot>(Bar->Slot))
		{
			BarSlot->SetAnchors(FAnchors(0.5f, 1.f));
			BarSlot->SetAlignment(FVector2D(0.5f, 1.f));
			BarSlot->SetPosition(FVector2D(0.f, -40.f));
			BarSlot->SetAutoSize(true);
		}
		HintText = WidgetTree->ConstructWidget<UTextBlock>();
		HintText->SetFont(SWGRetailStyle::Font(13));
		HintText->SetColorAndOpacity(FSlateColor(HintColor));
		HintText->SetShadowOffset(FVector2D(1.f, 1.f));
		HintText->SetText(NSLOCTEXT("SWGEmu", "HoloInventoryHint", "Hover for details, click to pin  •  Right-click for options  •  Wheel scrolls the bag  •  Drag to turn"));
		if (UHorizontalBoxSlot* HintSlot = Bar->AddChildToHorizontalBox(HintText))
		{
			HintSlot->SetVerticalAlignment(VAlign_Center);
			HintSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
		}
		WindowButton = MakeHintButton(WidgetTree, Bar, NSLOCTEXT("SWGEmu", "HoloInventoryWindow", "Window Inventory"));
		CloseButton = MakeHintButton(WidgetTree, Bar, NSLOCTEXT("SWGEmu", "HoloInventoryClose", "Close"));
	}
	LabelLayer = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	BuildMissingOverlay();
	// The whole screen is the hologram's control surface.
	SetVisibility(ESlateVisibility::Visible);
	SetIsFocusable(true);
}

void USWGHoloInventoryWidget::BuildMissingOverlay()
{
	if (!LabelLayer)
	{
		return;
	}
	auto MakeText = [this](int32 Size, const FLinearColor& Color)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
		Text->SetFont(SWGHoloStyle::Font(Size));
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetShadowOffset(FVector2D(1.f, 1.f));
		return Text;
	};
	auto AddToLayer = [this](UWidget* Widget, int32 ZOrder)
	{
		if (UCanvasPanelSlot* WidgetSlot = LabelLayer->AddChildToCanvas(Widget))
		{
			WidgetSlot->SetAutoSize(true);
			WidgetSlot->SetZOrder(ZOrder);
		}
	};
	if (!BagFrame)
	{
		UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
		Frame->SetBrush(SWGHoloStyle::FrameBrush());
		BagFrame = Frame;
		AddToLayer(Frame, /*ZOrder=*/-1);
		// Sized each frame to the list rather than to its content.
		Cast<UCanvasPanelSlot>(Frame->Slot)->SetAutoSize(false);
	}
	// Headings on a panel of their own, so they read over the ground and tell the two groups apart.
	auto MakeCaption = [this, &MakeText, &AddToLayer](TObjectPtr<UTextBlock>& OutText, TObjectPtr<UWidget>& OutHolder)
	{
		if (OutText)
		{
			// The Blueprint's own; it holds itself unless a panel round it was bound too.
			if (!OutHolder)
			{
				OutHolder = OutText;
			}
			return;
		}
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrush(SWGHoloStyle::PanelBrush(false));
		Panel->SetPadding(FMargin(10.f, 3.f));
		OutText = MakeText(13, SWGHoloStyle::BrightText);
		Panel->SetContent(OutText);
		OutHolder = Panel;
		AddToLayer(Panel, 0);
	};
	MakeCaption(BagCaption, BagCaptionPanel);
	MakeCaption(EquippedCaption, EquippedCaptionPanel);
	for (TObjectPtr<UTextBlock>* More : { &BagMoreBefore, &BagMoreAfter })
	{
		if (!*More)
		{
			*More = MakeText(13, SWGHoloStyle::BrightText);
			AddToLayer(*More, 0);
		}
	}
	for (UWidget* Piece : { BagFrame.Get(), BagCaptionPanel.Get(), EquippedCaptionPanel.Get(), (UWidget*)BagMoreBefore.Get(), (UWidget*)BagMoreAfter.Get() })
	{
		Piece->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (!bApplyHoloStyle)
	{
		return;
	}
	// The holo font is a system face, not an asset, so a Blueprint can't pick it.
	for (UTextBlock* Text : { BagCaption.Get(), EquippedCaption.Get(), BagMoreBefore.Get(), BagMoreAfter.Get() })
	{
		Text->SetFont(SWGHoloStyle::Font(13));
		Text->SetColorAndOpacity(FSlateColor(SWGHoloStyle::BrightText));
	}
	for (UWidget* Holder : { BagCaptionPanel.Get(), EquippedCaptionPanel.Get() })
	{
		if (UBorder* Panel = Cast<UBorder>(Holder))
		{
			Panel->SetBrush(SWGHoloStyle::PanelBrush(false));
		}
	}
	if (UBorder* Frame = Cast<UBorder>(BagFrame))
	{
		Frame->SetBrush(SWGHoloStyle::FrameBrush());
	}
	if (HintText)
	{
		HintText->SetFont(SWGRetailStyle::Font(13));
		HintText->SetColorAndOpacity(FSlateColor(HintColor));
		HintText->SetShadowOffset(FVector2D(1.f, 1.f));
	}
}

void USWGHoloInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (WindowButton) { WindowButton->OnClicked.AddUniqueDynamic(this, &USWGHoloInventoryWidget::HandleWindowClicked); }
	if (CloseButton) { CloseButton->OnClicked.AddUniqueDynamic(this, &USWGHoloInventoryWidget::HandleCloseClicked); }
	if (ButtonTextTints.IsEmpty())
	{
		USWGTreSubsystem* Tre = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGTreSubsystem>() : nullptr;
		for (UButton* Button : { WindowButton.Get(), CloseButton.Get() })
		{
			if (UObject* TextTint = SWGRetailStyle::ApplyHudButton(Button, Tre))
			{
				ButtonTextTints.Add(TextTint);
			}
		}
	}
	Examine = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGExamineSubsystem>() : nullptr;
	if (Examine)
	{
		Examine->OnExamineInfo.AddUniqueDynamic(this, &USWGHoloInventoryWidget::HandleExamineInfo);
	}
	Project();
	RefreshItems();
	// Keyboard and gamepad both come here, so pad buttons don't also fire the action bar.
	SetFocus();
	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		SetUserFocus(PlayerController);
	}
}

void USWGHoloInventoryWidget::NativeDestruct()
{
	if (Examine)
	{
		Examine->OnExamineInfo.RemoveDynamic(this, &USWGHoloInventoryWidget::HandleExamineInfo);
	}
	RestoreView();
	Super::NativeDestruct();
}

void USWGHoloInventoryWidget::Project()
{
	ACharacter* Character = Cast<ACharacter>(GetOwningPlayerPawn());
	UWorld* World = GetWorld();
	FVector ProjectorLocation;
	if (!Character || !World || !FSWGHoloView::FindProjectorLocation(Character, ProjectorDistance, ProjectorHeight, ProjectorLocation, ProjectorSideOffset))
	{
		return;
	}
	const FRotator Facing(0.f, Character->GetActorRotation().Yaw, 0.f);
	const FVector CameraLocation = ProjectorLocation + Facing.RotateVector(CameraOffset);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	// The figure faces the camera, as a mirror would.
	const FRotator TowardCamera(0.f, (CameraLocation - ProjectorLocation).Rotation().Yaw, 0.f);
	Figure = World->SpawnActor<ASWGHoloFigureActor>(ASWGHoloFigureActor::StaticClass(), ProjectorLocation, TowardCamera, Params);
	if (!Figure)
	{
		return;
	}
	Figure->FigureScale = FigureScale;
	Figure->SetSource(Character);
	// A first guess at the turn that puts the figure at FigureScreenPosition, so
	// the blend lands close; SteerCamera settles the rest once it has arrived.
	constexpr float GuessHalfFieldOfView = 22.f;
	const FVector Focus = ProjectorLocation + FVector(0.f, 0.f, Figure->GetFigureHeight() * CameraAimHeight);
	const FVector ToFocus = Focus - CameraLocation;
	const FVector Right = FRotationMatrix(ToFocus.Rotation()).GetUnitAxis(EAxis::Y);
	const float Offset = (0.5f - FigureScreenPosition.X) * 2.f * FMath::Tan(FMath::DegreesToRadians(GuessHalfFieldOfView));
	View.Begin(*this, CameraLocation, Focus + Right * ToFocus.Size() * Offset, CameraFieldOfView, CameraBlendSeconds, HudOpacity);
	// Laid out against the camera every tick (LayoutBag); anywhere will do until then.
	Shelf = World->SpawnActor<ASWGHoloShelfActor>(ASWGHoloShelfActor::StaticClass(), ProjectorLocation, TowardCamera, Params);
}

void USWGHoloInventoryWidget::RestoreView()
{
	View.End(*this);
	if (Figure)
	{
		Figure->Destroy();
		Figure = nullptr;
	}
	if (Shelf)
	{
		Shelf->Destroy();
		Shelf = nullptr;
	}
}

void USWGHoloInventoryWidget::SteerCamera(const FGeometry& MyGeometry)
{
	ACameraActor* Camera = View.GetCamera();
	APlayerController* PlayerController = GetOwningPlayer();
	const FVector2D Size = MyGeometry.GetLocalSize();
	if (!Camera || !PlayerController || !Figure || Size.X <= 0.f)
	{
		return;
	}
	// Until the blend has arrived, what's on screen isn't this camera's view. Once
	// it has, it stays arrived; the camera moving itself below mustn't read as a blend.
	if (!bCameraArrived)
	{
		bCameraArrived = !PlayerController->PlayerCameraManager
			|| PlayerController->PlayerCameraManager->GetCameraLocation().Equals(Camera->GetActorLocation(), 1.f);
		if (!bCameraArrived)
		{
			return;
		}
	}
	const FVector Focus = Figure->GetActorLocation() + FVector(0.f, 0.f, Figure->GetFigureHeight() * CameraAimHeight);
	FVector2D Screen;
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PlayerController, Focus, Screen, /*bPlayerViewportRelative=*/true))
	{
		return;
	}
	// Turn part of the way each frame; the degrees-per-unit estimate needn't be
	// exact, as the next frame measures again.
	const FVector2D Error = Screen - FigureScreenPosition * Size;
	if (Error.SizeSquared() >= 1.f)
	{
		const float DegreesPerUnit = CameraFieldOfView * 0.7f / Size.X;
		FRotator Rotation = Camera->GetActorRotation();
		Rotation.Yaw += Error.X * DegreesPerUnit * 0.5f;
		Rotation.Pitch -= Error.Y * DegreesPerUnit * 0.5f;
		Camera->SetActorRotation(Rotation);
	}

	// Slide the camera until the player's own head sits at PlayerScreenPosition;
	// the turn above keeps the figure where it is meanwhile.
	const ACharacter* Character = Cast<ACharacter>(GetOwningPlayerPawn());
	const USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	FVector2D HeadScreen;
	if (!Body || !UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PlayerController, Body->GetSocketLocation(TEXT("head")), HeadScreen, true))
	{
		return;
	}
	const FVector2D HeadError = (HeadScreen - PlayerScreenPosition * Size) / Size;
	if (HeadError.SizeSquared() < FMath::Square(0.005f))
	{
		return;
	}
	// World units of slide per screen-width of error; a fraction of the distance to the head, so it eases in.
	const FVector HeadLocation = Body->GetSocketLocation(TEXT("head"));
	const float SlidePerScreen = FVector::Distance(Camera->GetActorLocation(), HeadLocation) * 0.15f;
	const FRotationMatrix Axes(Camera->GetActorRotation());
	FVector Next = Camera->GetActorLocation()
		+ Axes.GetUnitAxis(EAxis::Y) * HeadError.X * SlidePerScreen
		- Axes.GetUnitAxis(EAxis::Z) * HeadError.Y * SlidePerScreen;
	// Never so close the head fills the view, nor so far it's lost.
	constexpr float NearestToHead = 60.f;
	constexpr float FarthestFromHead = 300.f;
	const FVector FromHead = Next - HeadLocation;
	Next = HeadLocation + FromHead.GetSafeNormal() * FMath::Clamp(FromHead.Size(), NearestToHead, FarthestFromHead);
	Camera->SetActorLocation(Next);
}

void USWGHoloInventoryWidget::RebuildBagLabels()
{
	for (auto It = BagLabels.CreateIterator(); It; ++It)
	{
		if (!Contents.ContainsByPredicate([&It](const FSWGInventoryEntry& Entry) { return Entry.ObjectId == It->Key; }))
		{
			if (It->Value)
			{
				It->Value->RemoveFromParent();
			}
			It.RemoveCurrent();
		}
	}
	if (!LabelLayer)
	{
		return;
	}
	for (const FSWGInventoryEntry& Entry : Contents)
	{
		TObjectPtr<USWGHoloLabelWidget>& Label = BagLabels.FindOrAdd(Entry.ObjectId);
		if (!Label)
		{
			Label = USWGHoloLabelWidget::Create(GetOwningPlayer());
			Label->OnHovered.BindUObject(this, &USWGHoloInventoryWidget::HandleLabelHovered);
			Label->OnPressed.BindUObject(this, &USWGHoloInventoryWidget::HandleLabelPressed);
			if (UCanvasPanelSlot* LabelSlot = LabelLayer->AddChildToCanvas(Label))
			{
				LabelSlot->SetAutoSize(true);
			}
			Label->SetVisibility(ESlateVisibility::Hidden);
		}
		Label->SetItem(Entry.ObjectId, FText::FromString(Entry.Label()));
	}
}

void USWGHoloInventoryWidget::LayoutBag(const FGeometry& MyGeometry)
{
	APlayerController* PlayerController = GetOwningPlayer();
	const FVector2D Size = MyGeometry.GetLocalSize();
	if (!Shelf || !PlayerController || !BagCaption || Size.X <= 0.f)
	{
		return;
	}
	const float Left = BagArea.X * Size.X;
	const float Top = BagArea.Y * Size.Y;
	const float Right = BagArea.Z * Size.X;
	const float Bottom = BagArea.W * Size.Y;
	const int32 Columns = FMath::Max(1, BagColumns);
	const float CellWidth = (Right - Left) / Columns;
	const int32 Rows = FMath::Max(1, FMath::FloorToInt((Bottom - Top) / BagRowHeight));
	if (Shelf->GetColumns() != Columns || LastBagRows != Rows)
	{
		LastBagRows = Rows;
		Shelf->SetGridSize(Columns, Rows);
	}
	constexpr float NameGap = 8.f;
	const float ModelSize = BagRowHeight * 0.8f;

	// The grid in the world, from where three cell middles fall on screen, through
	// the view as rendered, at about the figure's depth so it reads as the same projection.
	const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
	const FVector ViewOrigin = PlayerController->PlayerCameraManager ? PlayerController->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	const FVector FigureMiddle = Figure ? Figure->GetActorLocation() + FVector(0.f, 0.f, Figure->GetFigureHeight() * 0.5f) : ViewOrigin;
	const float Depth = FMath::Max(50.f, FVector::Distance(ViewOrigin, FigureMiddle) * BagDepth);
	auto Deproject = [PlayerController, ViewportScale, Depth](const FVector2D& Local, FVector& OutWorld)
	{
		FVector Origin;
		FVector Direction;
		if (!PlayerController->DeprojectScreenPositionToWorld(Local.X * ViewportScale, Local.Y * ViewportScale, Origin, Direction))
		{
			return false;
		}
		OutWorld = Origin + Direction * Depth;
		return true;
	};
	const FVector2D FirstModel(Left + ModelSize * 0.5f, Top + BagRowHeight * 0.5f);
	FVector TopLeft, NextColumn, NextRow;
	if (!Deproject(FirstModel, TopLeft) || !Deproject(FirstModel + FVector2D(CellWidth, 0.f), NextColumn) || !Deproject(FirstModel + FVector2D(0.f, BagRowHeight), NextRow))
	{
		return;
	}
	const FVector RowStep = NextRow - TopLeft;
	Shelf->SetGridFrame(TopLeft, NextColumn - TopLeft, RowStep, RowStep.Size() * ModelSize / BagRowHeight);

	// A frame round the list and its heading, with room below for the "more" count.
	constexpr float FramePad = 8.f;
	constexpr float MoreLineHeight = 22.f;
	const float HeadingHeight = FMath::Max(BagCaptionPanel ? BagCaptionPanel->GetDesiredSize().Y : 0.f, 22.f);
	const float ListBottom = Top + Rows * BagRowHeight;
	BagFrameRect = Contents.IsEmpty() ? FBox2D(ForceInit)
		: FBox2D(FVector2D(Left - FramePad, Top - HeadingHeight - FramePad * 2.f), FVector2D(Right + FramePad, ListBottom + MoreLineHeight + FramePad));
	if (BagFrame)
	{
		BagFrame->SetVisibility(BagFrameRect.bIsValid ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* FrameSlot = Cast<UCanvasPanelSlot>(BagFrame->Slot); FrameSlot && BagFrameRect.bIsValid)
		{
			FrameSlot->SetAutoSize(false);
			FrameSlot->SetPosition(BagFrameRect.Min);
			FrameSlot->SetSize(BagFrameRect.GetSize());
		}
	}

	// The figure's droid projects the list too: a faint cone onto the frame's corners, and a brighter ray to the item in focus.
	if (Figure)
	{
		TArray<FVector> RayTargets;
		TArray<float> RayBrightness;
		if (BagFrameRect.bIsValid)
		{
			const FVector2D& Min = BagFrameRect.Min;
			const FVector2D& Max = BagFrameRect.Max;
			for (const FVector2D& Corner : { Min, FVector2D(Max.X, Min.Y), FVector2D(Min.X, Max.Y), Max })
			{
				FVector World;
				if (Deproject(Corner, World))
				{
					RayTargets.Add(World);
					RayBrightness.Add(BagRayBrightness);
				}
			}
		}
		const int64 Focused = HoveredShelfId != 0 ? HoveredShelfId : CardObjectId;
		FVector FocusedCentre;
		if (Focused != 0 && Shelf->GetItemCenter(Focused, FocusedCentre))
		{
			RayTargets.Add(FocusedCentre);
			RayBrightness.Add(BagItemRayBrightness);
		}
		Figure->SetExtraRays(RayTargets, RayBrightness);
	}

	// Names beside the models; each whole cell picks its item.
	BagCells.Reset();
	const TArray<int64> Visible = Shelf->GetVisibleItems();
	for (const TPair<int64, TObjectPtr<USWGHoloLabelWidget>>& Pair : BagLabels)
	{
		USWGHoloLabelWidget* Label = Pair.Value;
		FVector Centre;
		FVector2D Screen;
		if (!Label || !Visible.Contains(Pair.Key) || !Shelf->GetItemCenter(Pair.Key, Centre)
			|| !UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PlayerController, Centre, Screen, /*bPlayerViewportRelative=*/true))
		{
			if (Label)
			{
				Label->SetVisibility(ESlateVisibility::Hidden);
			}
			continue;
		}
		const float CellLeft = Screen.X - ModelSize * 0.5f;
		BagCells.Add(Pair.Key, FBox2D(FVector2D(CellLeft, Screen.Y - BagRowHeight * 0.5f), FVector2D(CellLeft + CellWidth, Screen.Y + BagRowHeight * 0.5f)));
		Label->SetWrapWidth(CellWidth - ModelSize - NameGap * 3.f);
		if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(Label->Slot))
		{
			LabelSlot->SetAlignment(FVector2D(0.f, 0.5f));
			LabelSlot->SetPosition(FVector2D(Screen.X + ModelSize * 0.5f + NameGap, Screen.Y));
		}
		Label->SetVisibility(ESlateVisibility::Visible);
	}

	// The heading over the list, and how many more lie above and below it.
	auto Place = [](UWidget* Positioned, UTextBlock* Text, const FVector2D& Position, const FVector2D& Alignment, const FText& Content)
	{
		Text->SetText(Content);
		Positioned->SetVisibility(Content.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		if (UCanvasPanelSlot* PositionedSlot = Cast<UCanvasPanelSlot>(Positioned->Slot))
		{
			PositionedSlot->SetAlignment(Alignment);
			PositionedSlot->SetPosition(Position);
		}
	};
	constexpr float HeadingGap = 6.f;
	Place(BagCaptionPanel, BagCaption, FVector2D(Left, Top - HeadingGap), FVector2D(0.f, 1.f),
		FText::Format(NSLOCTEXT("SWGEmu", "HoloBagCaption", "In your inventory  ({0})"), FText::AsNumber(Contents.Num())));
	const int32 Before = Shelf->CountHiddenBefore();
	const int32 After = Shelf->CountHiddenAfter();
	Place(BagMoreBefore, BagMoreBefore, FVector2D(Right, Top - HeadingGap), FVector2D(1.f, 1.f),
		Before > 0 ? FText::Format(NSLOCTEXT("SWGEmu", "HoloBagBefore", "▲ {0} more"), FText::AsNumber(Before)) : FText::GetEmpty());
	Place(BagMoreAfter, BagMoreAfter, FVector2D(Right, ListBottom + HeadingGap), FVector2D(1.f, 0.f),
		After > 0 ? FText::Format(NSLOCTEXT("SWGEmu", "HoloBagAfter", "▼ {0} more"), FText::AsNumber(After)) : FText::GetEmpty());
}

int64 USWGHoloInventoryWidget::PickShelfItem(const FVector2D& LocalPosition) const
{
	for (const TPair<int64, FBox2D>& Cell : BagCells)
	{
		if (Cell.Value.IsInside(LocalPosition))
		{
			return Cell.Key;
		}
	}
	return 0;
}

void USWGHoloInventoryWidget::ScrollBag(int32 Rows)
{
	if (Shelf)
	{
		Shelf->Scroll(Rows);
	}
}

void USWGHoloInventoryWidget::SelectNextInBag(int32 Direction)
{
	if (!Shelf || Shelf->GetItems().IsEmpty())
	{
		return;
	}
	const TArray<int64>& Bag = Shelf->GetItems();
	const int32 Current = Bag.IndexOfByKey(PinnedObjectId);
	const int32 Next = Current == INDEX_NONE ? (Direction >= 0 ? 0 : Bag.Num() - 1) : FMath::Clamp(Current + (Direction >= 0 ? 1 : -1), 0, Bag.Num() - 1);
	PinnedObjectId = Bag[Next];
	Shelf->ScrollTo(PinnedObjectId);
	UpdateCard();
}

void USWGHoloInventoryWidget::Close()
{
	RestoreView();
	RemoveFromParent();
	OnClosed.Broadcast();
}

void USWGHoloInventoryWidget::SwitchToWindow()
{
	OnSwitchToWindow.Broadcast();
}

void USWGHoloInventoryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!Figure)
	{
		return;
	}
	if (FMath::Abs(TurnStick) >= AnalogDeadZone)
	{
		Figure->SetFigureYaw(Figure->GetFigureYaw() + TurnStick * AnalogTurnSpeed * InDeltaTime);
	}
	SteerCamera(MyGeometry);
	if (FPlatformTime::Seconds() >= NextRefreshTime)
	{
		RefreshItems();
	}
	LayoutBag(MyGeometry);
	LayoutMarkers(MyGeometry);
	UpdateCard();
	PlaceCard(MyGeometry);
}

void USWGHoloInventoryWidget::RefreshItems()
{
	NextRefreshTime = FPlatformTime::Seconds() + RefreshInterval;
	if (SWGInventoryQuery::Gather(GetGameInstance(), Equipped, Contents))
	{
		RebuildMarkers();
		RebuildBagLabels();
		if (Shelf)
		{
			TArray<int64> BagIds;
			for (const FSWGInventoryEntry& Entry : Contents)
			{
				BagIds.Add(Entry.ObjectId);
			}
			Shelf->SetItems(BagIds);
		}
	}
	// Meshes land after the containment that names them; pick up any that have arrived.
	for (FItemMarker& Marker : Markers)
	{
		if (!Marker.Visual.IsValid())
		{
			Marker.Visual = FindItemVisual(Marker.Entry.ObjectId);
		}
	}
}

UPrimitiveComponent* USWGHoloInventoryWidget::FindItemVisual(int64 ObjectId) const
{
	const APawn* Pawn = GetOwningPlayerPawn();
	const USWGEquipmentComponent* Equipment = Pawn ? Pawn->FindComponentByClass<USWGEquipmentComponent>() : nullptr;
	return Equipment ? Equipment->FindItemVisual(ObjectId) : nullptr;
}

void USWGHoloInventoryWidget::RebuildMarkers()
{
	// Keep each item's column across a rebuild, so equipping one thing doesn't reshuffle the rest.
	TMap<int64, bool> PlacedSides;
	for (const FItemMarker& Marker : Markers)
	{
		if (Marker.bSidePlaced)
		{
			PlacedSides.Add(Marker.Entry.ObjectId, Marker.bLeft);
		}
	}
	for (USWGHoloLabelWidget* Label : Labels)
	{
		if (Label)
		{
			Label->RemoveFromParent();
		}
	}
	Labels.Reset();
	Markers.Reset();
	if (!LabelLayer)
	{
		return;
	}
	for (const FSWGInventoryEntry& Entry : Equipped)
	{
		FItemMarker& Marker = Markers.AddDefaulted_GetRef();
		Marker.Entry = Entry;
		Marker.Visual = FindItemVisual(Entry.ObjectId);
		if (const bool* bLeft = PlacedSides.Find(Entry.ObjectId))
		{
			Marker.bLeft = *bLeft;
			Marker.bSidePlaced = true;
		}
		USWGHoloLabelWidget* Label = USWGHoloLabelWidget::Create(GetOwningPlayer());
		Label->SetItem(Entry.ObjectId, FText::FromString(Entry.Label()));
		Label->SetLit(Entry.ObjectId == HoveredObjectId);
		Label->OnHovered.BindUObject(this, &USWGHoloInventoryWidget::HandleLabelHovered);
		Label->OnPressed.BindUObject(this, &USWGHoloInventoryWidget::HandleLabelPressed);
		if (UCanvasPanelSlot* LabelSlot = LabelLayer->AddChildToCanvas(Label))
		{
			LabelSlot->SetAutoSize(true);
		}
		// Hidden until the first layout has somewhere to put it.
		Label->SetVisibility(ESlateVisibility::Hidden);
		Marker.LabelIndex = Labels.Add(Label);
	}
}

void USWGHoloInventoryWidget::LayoutMarkers(const FGeometry& MyGeometry)
{
	APlayerController* PlayerController = GetOwningPlayer();
	if (!Figure || !PlayerController)
	{
		return;
	}
	auto ProjectToWidget = [PlayerController](const FVector& World, FVector2D& OutLocal)
	{
		return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PlayerController, World, OutLocal, /*bPlayerViewportRelative=*/true);
	};
	FVector2D Centre;
	if (!ProjectToWidget(Figure->GetActorLocation() + FVector(0.f, 0.f, Figure->GetFigureHeight() * 0.5f), Centre))
	{
		return;
	}
	const FVector2D Size = MyGeometry.GetLocalSize();
	constexpr float ElbowRun = 18.f;
	// Arms reach about a fifth of the height either side; the columns clear that
	// and every marker, however big the figure is drawn.
	constexpr float HalfWidthOfHeight = 0.2f;
	float ColumnGap = 0.f;
	if (const ACameraActor* Camera = View.GetCamera())
	{
		const FVector Across = FRotationMatrix(Camera->GetActorRotation()).GetUnitAxis(EAxis::Y);
		const FVector Middle = Figure->GetActorLocation() + FVector(0.f, 0.f, Figure->GetFigureHeight() * 0.5f);
		FVector2D Edge;
		if (ProjectToWidget(Middle + Across * Figure->GetFigureHeight() * HalfWidthOfHeight, Edge))
		{
			ColumnGap = FMath::Abs(Edge.X - Centre.X);
		}
	}
	// Clear of the hint bar along the bottom.
	constexpr float BottomMargin = 80.f;

	TArray<int32> Columns[2];
	for (int32 Index = 0; Index < Markers.Num(); ++Index)
	{
		FItemMarker& Marker = Markers[Index];
		Marker.bOnScreen = ProjectToWidget(Figure->GetItemAnchor(Marker.Entry.SlotNames, Marker.Visual.Get()), Marker.Anchor);
		if (!Marker.bOnScreen)
		{
			continue;
		}
		if (!Marker.bSidePlaced)
		{
			// Near the middle line (head, chest, belt) go wherever there are fewer so far.
			const float Offset = Marker.Anchor.X - Centre.X;
			Marker.bLeft = FMath::Abs(Offset) > 6.f ? Offset < 0.f : Columns[0].Num() <= Columns[1].Num();
			Marker.bSidePlaced = true;
		}
		Columns[Marker.bLeft ? 0 : 1].Add(Index);
		ColumnGap = FMath::Max(ColumnGap, FMath::Abs(Marker.Anchor.X - Centre.X));
	}
	ColumnGap += LabelColumnMargin;

	for (int32 Side = 0; Side < 2; ++Side)
	{
		TArray<int32>& Column = Columns[Side];
		const bool bLeft = Side == 0;
		Column.Sort([this](int32 Left, int32 Right)
		{
			return Markers[Left].Anchor.Y != Markers[Right].Anchor.Y ? Markers[Left].Anchor.Y < Markers[Right].Anchor.Y : Left < Right;
		});
		// Each name as level with its item as it can be without overlapping the one above.
		TArray<float> Centres;
		TArray<float> Heights;
		float PreviousBottom = -FLT_MAX;
		for (const int32 Index : Column)
		{
			const USWGHoloLabelWidget* Label = Labels[Markers[Index].LabelIndex];
			const float Height = FMath::Max(Label->GetDesiredSize().Y, 16.f);
			const float CentreY = FMath::Max(Markers[Index].Anchor.Y, PreviousBottom + LabelSpacing + Height * 0.5f);
			Centres.Add(CentreY);
			Heights.Add(Height);
			PreviousBottom = CentreY + Height * 0.5f;
		}
		// Too many to fit under the figure: slide the whole column up.
		const float Overflow = PreviousBottom - (Size.Y - BottomMargin);
		const float ColumnX = Centre.X + (bLeft ? -ColumnGap : ColumnGap);
		// Each name stays on screen on the left and clear of the bag's list on the
		// right, even if that brings it in over the figure.
		constexpr float EdgeMargin = 12.f;
		const float RightLimit = BagArea.X * Size.X - EdgeMargin;
		for (int32 Row = 0; Row < Column.Num(); ++Row)
		{
			FItemMarker& Marker = Markers[Column[Row]];
			const float CentreY = Centres[Row] - FMath::Max(0.f, Overflow);
			USWGHoloLabelWidget* Label = Labels[Marker.LabelIndex];
			const float LabelWidth = Label->GetDesiredSize().X;
			const float EdgeX = bLeft ? FMath::Max(ColumnX, EdgeMargin + LabelWidth) : FMath::Min(ColumnX, RightLimit - LabelWidth);
			Marker.LabelEdge = FVector2D(EdgeX, CentreY);
			Marker.Elbow = FVector2D(EdgeX + (bLeft ? ElbowRun : -ElbowRun), CentreY);
			if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(Label->Slot))
			{
				LabelSlot->SetAlignment(FVector2D(bLeft ? 1.f : 0.f, 0.5f));
				LabelSlot->SetPosition(Marker.LabelEdge);
			}
			Label->SetVisibility(ESlateVisibility::Visible);
		}
	}
	// "Equipped" centred over the figure's head, lifted clear of the droid when it hovers there.
	FVector2D Crown = FVector2D::ZeroVector;
	const bool bCrownShowing = !Markers.IsEmpty() && ProjectToWidget(Figure->GetActorLocation() + FVector(0.f, 0.f, Figure->GetFigureHeight()), Crown);
	if (EquippedCaptionPanel)
	{
		EquippedCaptionPanel->SetVisibility(bCrownShowing ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		EquippedCaption->SetText(FText::Format(NSLOCTEXT("SWGEmu", "HoloEquippedCaption", "Equipped  ({0})"), FText::AsNumber(Markers.Num())));
		UCanvasPanelSlot* CaptionSlot = Cast<UCanvasPanelSlot>(EquippedCaptionPanel->Slot);
		if (bCrownShowing && CaptionSlot)
		{
			constexpr float CrownGap = 10.f;
			constexpr float DroidClearRadius = 30.f;
			float CaptionBottom = Crown.Y - CrownGap;
			FVector2D Droid;
			const float HalfWidth = EquippedCaptionPanel->GetDesiredSize().X * 0.5f;
			if (ProjectToWidget(Figure->GetDroidLocation(), Droid) && FMath::Abs(Droid.X - Centre.X) < HalfWidth + DroidClearRadius)
			{
				CaptionBottom = FMath::Min(CaptionBottom, Droid.Y - DroidClearRadius);
			}
			CaptionSlot->SetAlignment(FVector2D(0.5f, 1.f));
			CaptionSlot->SetPosition(FVector2D(Centre.X, CaptionBottom));
		}
	}
	for (const FItemMarker& Marker : Markers)
	{
		if (!Marker.bOnScreen && Labels.IsValidIndex(Marker.LabelIndex))
		{
			Labels[Marker.LabelIndex]->SetVisibility(ESlateVisibility::Hidden);
		}
	}
}

int32 USWGHoloInventoryWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 MaxLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	constexpr float MarkerRadius = 3.5f;
	for (const FItemMarker& Marker : Markers)
	{
		if (!Marker.bOnScreen)
		{
			continue;
		}
		const bool bLit = Marker.Entry.ObjectId == HoveredObjectId || Marker.Entry.ObjectId == PinnedObjectId;
		const FLinearColor Color = bLit ? SWGHoloStyle::BrightLine : SWGHoloStyle::Line;
		const float Thickness = bLit ? 2.f : 1.4f;
		// A diamond on the item with a bright centre, then a line out to its name.
		const FVector2D& Point = Marker.Anchor;
		const TArray<FVector2D> Diamond = {
			Point + FVector2D(0.f, -MarkerRadius), Point + FVector2D(MarkerRadius, 0.f),
			Point + FVector2D(0.f, MarkerRadius), Point + FVector2D(-MarkerRadius, 0.f), Point + FVector2D(0.f, -MarkerRadius) };
		const TArray<FVector2D> Core = { Point + FVector2D(-1.f, 0.f), Point + FVector2D(1.f, 0.f) };
		// Leave the marker's own diamond clear.
		const FVector2D Start = Point + (Marker.Elbow - Point).GetSafeNormal() * MarkerRadius;
		const TArray<FVector2D> Leader = { Start, Marker.Elbow, Marker.LabelEdge };
		// A dark underlay first, so cyan lines still read where they cross the cyan figure.
		for (const TArray<FVector2D>* Shape : { &Diamond, &Leader })
		{
			FSlateDrawElement::MakeLines(OutDrawElements, MaxLayer + 1, AllottedGeometry.ToPaintGeometry(), *Shape, ESlateDrawEffect::None, LineUnderlay, true, Thickness + 2.f);
			FSlateDrawElement::MakeLines(OutDrawElements, MaxLayer + 2, AllottedGeometry.ToPaintGeometry(), *Shape, ESlateDrawEffect::None, Color, true, Thickness);
		}
		FSlateDrawElement::MakeLines(OutDrawElements, MaxLayer + 2, AllottedGeometry.ToPaintGeometry(), Core, ESlateDrawEffect::None, SWGHoloStyle::BrightLine, true, 2.5f);
	}
	// Bright brackets on the bag frame's corners, where the droid's rays land.
	if (BagFrameRect.bIsValid)
	{
		constexpr float Arm = 14.f;
		const FVector2D& Min = BagFrameRect.Min;
		const FVector2D& Max = BagFrameRect.Max;
		const TPair<FVector2D, FVector2D> Corners[] = {
			{ Min, FVector2D(1.f, 1.f) }, { FVector2D(Max.X, Min.Y), FVector2D(-1.f, 1.f) },
			{ FVector2D(Min.X, Max.Y), FVector2D(1.f, -1.f) }, { Max, FVector2D(-1.f, -1.f) } };
		for (const TPair<FVector2D, FVector2D>& Corner : Corners)
		{
			const TArray<FVector2D> Bracket = {
				Corner.Key + FVector2D(Corner.Value.X * Arm, 0.f), Corner.Key, Corner.Key + FVector2D(0.f, Corner.Value.Y * Arm) };
			FSlateDrawElement::MakeLines(OutDrawElements, MaxLayer + 2, AllottedGeometry.ToPaintGeometry(), Bracket, ESlateDrawEffect::None, SWGHoloStyle::BrightLine, true, 2.f);
		}
	}
	return MaxLayer + 2;
}

void USWGHoloInventoryWidget::HandleLabelHovered(int64 ObjectId, bool bHovered)
{
	if (!bHovered && ObjectId != HoveredObjectId)
	{
		return;
	}
	HoveredObjectId = bHovered ? ObjectId : 0;
	if (!bHovered)
	{
		HoverLostTime = FPlatformTime::Seconds();
	}
	UpdateCard();
}

void USWGHoloInventoryWidget::UpdateCard()
{
	int64 Wanted = HoveredObjectId != 0 ? HoveredObjectId : HoveredShelfId != 0 ? HoveredShelfId : PinnedObjectId;
	if (Wanted == 0 && FPlatformTime::Seconds() - HoverLostTime < CardLingerSeconds)
	{
		Wanted = CardObjectId;
	}
	if (Wanted != CardObjectId)
	{
		ShowCard(Wanted);
	}
	for (USWGHoloLabelWidget* Label : Labels)
	{
		if (Label)
		{
			Label->SetLit(Label->GetObjectId() == PinnedObjectId);
		}
	}
	for (const TPair<int64, TObjectPtr<USWGHoloLabelWidget>>& Pair : BagLabels)
	{
		if (Pair.Value)
		{
			// The bag's names light for the hovered model too, not just their own hover.
			Pair.Value->SetLit(Pair.Key == PinnedObjectId || Pair.Key == HoveredShelfId);
		}
	}
	if (Card)
	{
		Card->SetPinned(CardObjectId != 0 && CardObjectId == PinnedObjectId);
	}
}

void USWGHoloInventoryWidget::ShowCard(int64 ObjectId)
{
	CardObjectId = ObjectId;
	// Light the item wherever it is: on the figure, or up off its pad in the strip.
	const bool bInBag = Shelf && Shelf->GetItems().Contains(ObjectId);
	if (Figure)
	{
		Figure->SetHighlighted(ObjectId != 0 && !bInBag ? FindItemVisual(ObjectId) : nullptr);
	}
	if (Shelf)
	{
		Shelf->SetHovered(bInBag ? ObjectId : 0);
	}
	if (ObjectId == 0)
	{
		if (Card)
		{
			Card->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}
	if (!Card && LabelLayer)
	{
		Card = USWGHoloDetailCardWidget::Create(GetOwningPlayer());
		if (UCanvasPanelSlot* CardSlot = LabelLayer->AddChildToCanvas(Card))
		{
			CardSlot->SetAutoSize(true);
			CardSlot->SetZOrder(10);
		}
	}
	if (!Card)
	{
		return;
	}
	// The server's half is only asked for once per item; the client's half is there at once.
	FSWGExamineInfo Info;
	if (const FSWGExamineInfo* Cached = ExamineCache.Find(ObjectId))
	{
		Info = *Cached;
	}
	else if (Examine && Examine->Describe(ObjectId, Info))
	{
		ExamineCache.Add(ObjectId, Info);
	}
	else
	{
		Info.ObjectId = ObjectId;
		Info.Name = SWGInventoryQuery::Describe(GetGameInstance(), ObjectId).Name;
	}
	Card->SetInfo(Info);
	const FSWGInventoryEntry* Worn = Equipped.FindByPredicate([ObjectId](const FSWGInventoryEntry& Entry) { return Entry.ObjectId == ObjectId; });
	Card->SetStatus(Worn
		? (Worn->SlotNames.IsEmpty() ? NSLOCTEXT("SWGEmu", "HoloCardEquipped", "Equipped")
			: FText::Format(NSLOCTEXT("SWGEmu", "HoloCardEquippedSlots", "Equipped  •  {0}"), FText::FromString(Worn->SlotNames)))
		: bInBag ? NSLOCTEXT("SWGEmu", "HoloCardInBag", "In your inventory") : FText::GetEmpty());
	// Hidden (not collapsed) for a frame: it needs a size before it can be placed.
	Card->SetVisibility(ESlateVisibility::Hidden);
	Card->PlayOpen();
}

void USWGHoloInventoryWidget::HandleExamineInfo(const FSWGExamineInfo& Info)
{
	if (!ExamineCache.Contains(Info.ObjectId) && !Markers.ContainsByPredicate([&Info](const FItemMarker& Marker) { return Marker.Entry.ObjectId == Info.ObjectId; })
		&& !(Shelf && Shelf->GetItems().Contains(Info.ObjectId)))
	{
		return;
	}
	ExamineCache.Add(Info.ObjectId, Info);
	if (Card && Info.ObjectId == CardObjectId)
	{
		Card->SetInfo(Info);
	}
}

void USWGHoloInventoryWidget::PlaceCard(const FGeometry& MyGeometry)
{
	if (!Card || CardObjectId == 0)
	{
		return;
	}
	if (Shelf && Shelf->GetItems().Contains(CardObjectId))
	{
		// Just left of the list, level with the item's row, so it never hides the rest of the list.
		const FBox2D* Cell = BagCells.Find(CardObjectId);
		if (!Cell)
		{
			Card->SetVisibility(ESlateVisibility::Collapsed);
			return;
		}
		constexpr float Gap = 12.f;
		constexpr float Margin = 12.f;
		constexpr float BottomMargin = 80.f;
		const FVector2D CardSize = Card->GetDesiredSize();
		const FVector2D Size = MyGeometry.GetLocalSize();
		const FVector2D Position(
			FMath::Clamp(BagArea.X * Size.X - Gap - CardSize.X, Margin, FMath::Max(Margin, Size.X - Margin - CardSize.X)),
			FMath::Clamp(Cell->Min.Y, Margin, FMath::Max(Margin, Size.Y - BottomMargin - CardSize.Y)));
		if (UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(Card->Slot))
		{
			CardSlot->SetAlignment(FVector2D::ZeroVector);
			CardSlot->SetPosition(Position);
		}
		if (CardSize.X > 0.f)
		{
			Card->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		return;
	}
	const FItemMarker* Marker = Markers.FindByPredicate([this](const FItemMarker& Candidate) { return Candidate.Entry.ObjectId == CardObjectId; });
	if (!Marker || !Marker->bOnScreen || !Labels.IsValidIndex(Marker->LabelIndex))
	{
		Card->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	// Beside the name on the far side from the figure; where the screen is too
	// narrow for that, under the name (over it if there's no room below), outer
	// edges lined up. Kept on screen either way.
	constexpr float Gap = 10.f;
	constexpr float ScreenMargin = 12.f;
	constexpr float BottomMargin = 80.f;
	const FVector2D LabelSize = Labels[Marker->LabelIndex]->GetDesiredSize();
	const FVector2D CardSize = Card->GetDesiredSize();
	const FVector2D Size = MyGeometry.GetLocalSize();
	const float LabelTop = Marker->LabelEdge.Y - LabelSize.Y * 0.5f;
	const float LabelOuterX = Marker->bLeft ? Marker->LabelEdge.X - LabelSize.X : Marker->LabelEdge.X + LabelSize.X;
	const float Bottom = Size.Y - BottomMargin;
	FVector2D Position;
	const bool bFitsBeside = Marker->bLeft ? LabelOuterX - Gap - CardSize.X >= ScreenMargin : LabelOuterX + Gap + CardSize.X <= Size.X - ScreenMargin;
	if (bFitsBeside)
	{
		Position = FVector2D(Marker->bLeft ? LabelOuterX - Gap - CardSize.X : LabelOuterX + Gap, LabelTop);
	}
	else
	{
		const float Below = LabelTop + LabelSize.Y + Gap * 0.5f;
		const float Above = LabelTop - Gap * 0.5f - CardSize.Y;
		Position = FVector2D(Marker->bLeft ? LabelOuterX : LabelOuterX - CardSize.X, Below + CardSize.Y <= Bottom || Above < ScreenMargin ? Below : Above);
	}
	Position.X = FMath::Clamp(Position.X, ScreenMargin, FMath::Max(ScreenMargin, Size.X - ScreenMargin - CardSize.X));
	Position.Y = FMath::Clamp(Position.Y, ScreenMargin, FMath::Max(ScreenMargin, Bottom - CardSize.Y));
	if (UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(Card->Slot))
	{
		CardSlot->SetAlignment(FVector2D::ZeroVector);
		CardSlot->SetPosition(Position);
	}
	if (CardSize.X > 0.f)
	{
		Card->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void USWGHoloInventoryWidget::SelectNext(int32 Direction)
{
	TArray<const FItemMarker*> Order;
	for (const FItemMarker& Marker : Markers)
	{
		if (Marker.bOnScreen)
		{
			Order.Add(&Marker);
		}
	}
	if (Order.IsEmpty())
	{
		return;
	}
	Order.Sort([](const FItemMarker& Left, const FItemMarker& Right)
	{
		return Left.bLeft != Right.bLeft ? Left.bLeft : Left.LabelEdge.Y < Right.LabelEdge.Y;
	});
	const int32 Current = Order.IndexOfByPredicate([this](const FItemMarker* Marker) { return Marker->Entry.ObjectId == PinnedObjectId; });
	const int32 Next = Current == INDEX_NONE ? (Direction >= 0 ? 0 : Order.Num() - 1) : (Current + (Direction >= 0 ? 1 : -1) + Order.Num()) % Order.Num();
	PinnedObjectId = Order[Next]->Entry.ObjectId;
	UpdateCard();
}

void USWGHoloInventoryWidget::HandleLabelPressed(int64 ObjectId, FKey Button, FVector2D ScreenPosition)
{
	if (Button == EKeys::LeftMouseButton)
	{
		PinnedObjectId = PinnedObjectId == ObjectId ? 0 : ObjectId;
		UpdateCard();
		return;
	}
	if (Button != EKeys::RightMouseButton)
	{
		return;
	}
	// The radial wants viewport pixels; the pointer event's position is desktop-absolute, so ask the controller.
	APlayerController* PlayerController = GetOwningPlayer();
	USWGRadialMenuSubsystem* Radial = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGRadialMenuSubsystem>() : nullptr;
	float MouseX = 0.f, MouseY = 0.f;
	if (Radial && PlayerController && PlayerController->GetMousePosition(MouseX, MouseY))
	{
		Radial->RequestMenu(ObjectId, FVector2D(MouseX, MouseY));
	}
}

FReply USWGHoloInventoryWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// A bag item takes the click the way a name does; anywhere else starts turning the figure.
	if (HoveredShelfId != 0)
	{
		HandleLabelPressed(HoveredShelfId, InMouseEvent.GetEffectingButton(), InMouseEvent.GetScreenSpacePosition());
		return FReply::Handled().SetUserFocus(TakeWidget(), EFocusCause::Mouse);
	}
	bTurning = InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton || InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton;
	return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

FReply USWGHoloInventoryWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (Figure && bTurning)
	{
		Figure->SetFigureYaw(Figure->GetFigureYaw() - InMouseEvent.GetCursorDelta().X * TurnDegreesPerPixel);
		return FReply::Handled();
	}
	const int64 Picked = PickShelfItem(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
	if (Picked != HoveredShelfId)
	{
		if (Picked == 0)
		{
			HoverLostTime = FPlatformTime::Seconds();
		}
		HoveredShelfId = Picked;
		UpdateCard();
	}
	return FReply::Handled();
}

FReply USWGHoloInventoryWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	ScrollBag(InMouseEvent.GetWheelDelta() > 0.f ? -1 : 1);
	return FReply::Handled();
}

void USWGHoloInventoryWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	if (HoveredShelfId != 0)
	{
		HoveredShelfId = 0;
		HoverLostTime = FPlatformTime::Seconds();
	}
}

FReply USWGHoloInventoryWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	bTurning = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply USWGHoloInventoryWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == ToggleKey || Key == EKeys::Gamepad_FaceButton_Right)
	{
		Close();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Top)
	{
		SwitchToWindow();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_DPad_Up)
	{
		SelectNext(Key == EKeys::Gamepad_DPad_Down ? 1 : -1);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_DPad_Left)
	{
		SelectNextInBag(Key == EKeys::Gamepad_DPad_Right ? 1 : -1);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_RightShoulder || Key == EKeys::Gamepad_LeftShoulder)
	{
		ScrollBag(Key == EKeys::Gamepad_RightShoulder ? 1 : -1);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Bottom && PinnedObjectId != 0)
	{
		// The radial opens where the selected name or bag item is, in viewport pixels.
		const FItemMarker* Marker = Markers.FindByPredicate([this](const FItemMarker& Candidate) { return Candidate.Entry.ObjectId == PinnedObjectId; });
		USWGRadialMenuSubsystem* Radial = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGRadialMenuSubsystem>() : nullptr;
		FVector2D Where = Marker ? Marker->LabelEdge : FVector2D::ZeroVector;
		FVector BagItem;
		const bool bFound = Marker != nullptr
			|| (Shelf && Shelf->GetItemCenter(PinnedObjectId, BagItem) && UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(), BagItem, Where, true));
		if (bFound && Radial)
		{
			Radial->RequestMenu(PinnedObjectId, Where * UWidgetLayoutLibrary::GetViewportScale(this));
		}
		return FReply::Handled();
	}
	// The sticks are read as analog; swallow their digital echoes so they don't reach the game.
	if (Key.IsAnalog() || Key == EKeys::Gamepad_RightStick_Left || Key == EKeys::Gamepad_RightStick_Right
		|| Key == EKeys::Gamepad_LeftStick_Left || Key == EKeys::Gamepad_LeftStick_Right
		|| Key == EKeys::Gamepad_LeftStick_Up || Key == EKeys::Gamepad_LeftStick_Down
		|| Key == EKeys::Gamepad_RightStick_Up || Key == EKeys::Gamepad_RightStick_Down)
	{
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply USWGHoloInventoryWidget::NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent)
{
	const FKey Key = InAnalogEvent.GetKey();
	if (Key == EKeys::Gamepad_RightX)
	{
		TurnStick = InAnalogEvent.GetAnalogValue();
		return FReply::Handled();
	}
	// Held here, so the pawn's own stick bindings don't walk or turn it meanwhile.
	if (Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_LeftY || Key == EKeys::Gamepad_RightY)
	{
		return FReply::Handled();
	}
	return Super::NativeOnAnalogValueChanged(InGeometry, InAnalogEvent);
}

void USWGHoloInventoryWidget::HandleWindowClicked() { SwitchToWindow(); }
void USWGHoloInventoryWidget::HandleCloseClicked() { Close(); }
