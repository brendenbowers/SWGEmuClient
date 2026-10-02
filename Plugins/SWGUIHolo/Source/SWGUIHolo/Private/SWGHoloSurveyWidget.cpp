#include "SWGHoloSurveyWidget.h"
#include "SWGHoloProjector.h"
#include "SWGHoloSurveyActor.h"
#include "SWGHoloSurveyFieldActor.h"
#include "SWGRetailStyle.h"
#include "SWGSurveyStyle.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Subsystems/SWGSurveySubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"

namespace
{
	const FLinearColor SurveyHintColor = FLinearColor::FromSRGBColor(FColor(0x96, 0xF4, 0xFC));
	/** A click this close to a row's label, in viewport pixels, picks it. */
	constexpr float PickRadiusPixels = 45.f;

	UButton* MakeBarButton(UWidgetTree* Tree, UHorizontalBox* Row, const FText& Label)
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

	/**
	 * The C++ bar sits bottom centre over the faded HUD; while scanning the HUD is
	 * back at full strength and the action bar would cover it, so it moves to the
	 * top, where the (then hidden) status line was. A Blueprint's own layout is left alone.
	 */
	void PlaceButtonBar(UWidget* AnyButton, bool bTop)
	{
		UCanvasPanelSlot* BarSlot = AnyButton && AnyButton->GetParent() ? Cast<UCanvasPanelSlot>(AnyButton->GetParent()->Slot) : nullptr;
		if (BarSlot)
		{
			BarSlot->SetAnchors(FAnchors(0.5f, bTop ? 0.f : 1.f));
			BarSlot->SetAlignment(FVector2D(0.5f, bTop ? 0.f : 1.f));
			BarSlot->SetPosition(FVector2D(0.f, bTop ? 60.f : -40.f));
		}
	}

	void StyleHoloText(UTextBlock* Text, int32 Size)
	{
		if (Text)
		{
			Text->SetFont(SWGRetailStyle::Font(Size));
			Text->SetColorAndOpacity(FSlateColor(SurveyHintColor));
			Text->SetShadowOffset(FVector2D(1.f, 1.f));
		}
	}
}

void USWGHoloSurveyWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Root;

		StatusText = WidgetTree->ConstructWidget<UTextBlock>();
		StatusText->SetJustification(ETextJustify::Center);
		Root->AddChild(StatusText);
		if (UCanvasPanelSlot* StatusSlot = Cast<UCanvasPanelSlot>(StatusText->Slot))
		{
			StatusSlot->SetAnchors(FAnchors(0.5f, 0.f));
			StatusSlot->SetAlignment(FVector2D(0.5f, 0.f));
			StatusSlot->SetPosition(FVector2D(0.f, 60.f));
			StatusSlot->SetAutoSize(true);
		}

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
		if (UHorizontalBoxSlot* HintSlot = Bar->AddChildToHorizontalBox(HintText))
		{
			HintSlot->SetVerticalAlignment(VAlign_Center);
			HintSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
		}
		SurveyButton = MakeBarButton(WidgetTree, Bar, NSLOCTEXT("SWGEmu", "HoloSurveySurvey", "Survey"));
		SampleButton = MakeBarButton(WidgetTree, Bar, NSLOCTEXT("SWGEmu", "HoloSurveySample", "Sample"));
		WindowButton = MakeBarButton(WidgetTree, Bar, NSLOCTEXT("SWGEmu", "HoloSurveyWindow", "Window"));
		CloseButton = MakeBarButton(WidgetTree, Bar, NSLOCTEXT("SWGEmu", "HoloSurveyClose", "Close"));
	}
	// A Blueprint's texts too: the font is a system face, not an asset it could pick.
	if (bApplyHoloStyle)
	{
		StyleHoloText(StatusText, 16);
		StyleHoloText(HintText, 13);
	}
	SetVisibility(ESlateVisibility::Visible);
	SetIsFocusable(true);
}

void USWGHoloSurveyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	UGameInstance* GameInstance = GetGameInstance();
	SurveySubsystem = GameInstance ? GameInstance->GetSubsystem<USWGSurveySubsystem>() : nullptr;
	Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	if (SurveySubsystem)
	{
		SurveySubsystem->OnResourcesChanged.AddUniqueDynamic(this, &USWGHoloSurveyWidget::HandleResourcesChanged);
		SurveySubsystem->OnSurveyStateChanged.AddUniqueDynamic(this, &USWGHoloSurveyWidget::HandleSurveyStateChanged);
		SurveySubsystem->OnSurveyResultReceived.AddUniqueDynamic(this, &USWGHoloSurveyWidget::HandleResultReceived);
	}
	if (SurveyButton) { SurveyButton->OnClicked.AddUniqueDynamic(this, &USWGHoloSurveyWidget::HandleSurveyClicked); }
	if (SampleButton) { SampleButton->OnClicked.AddUniqueDynamic(this, &USWGHoloSurveyWidget::HandleSampleClicked); }
	if (WindowButton) { WindowButton->OnClicked.AddUniqueDynamic(this, &USWGHoloSurveyWidget::HandleWindowClicked); }
	if (CloseButton) { CloseButton->OnClicked.AddUniqueDynamic(this, &USWGHoloSurveyWidget::HandleCloseClicked); }
	if (ButtonTextTints.IsEmpty())
	{
		const TPair<UButton*, FString> Buttons[] = {
			{ SurveyButton.Get(), TEXT("@ui:res_survey") }, { SampleButton.Get(), TEXT("@ui:res_survey_sample") },
			{ WindowButton.Get(), FString() }, { CloseButton.Get(), FString() } };
		for (const TPair<UButton*, FString>& Entry : Buttons)
		{
			if (UObject* TextTint = SWGRetailStyle::ApplyHudButton(Entry.Key, Tre, Entry.Value))
			{
				ButtonTextTints.Add(TextTint);
			}
		}
	}
	if (HintText)
	{
		HintText->SetText(NSLOCTEXT("SWGEmu", "HoloSurveyHint", "Up/Down to pick  •  Enter to open/survey  •  Backspace to go back"));
	}
	Project();
	RefreshEntries();
	RefreshStatus();
	// Keyboard and gamepad both come here, so pad buttons don't also fire the action bar.
	SetFocus();
	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		SetUserFocus(PlayerController);
	}
}

void USWGHoloSurveyWidget::NativeDestruct()
{
	if (SurveySubsystem)
	{
		SurveySubsystem->OnResourcesChanged.RemoveAll(this);
		SurveySubsystem->OnSurveyStateChanged.RemoveAll(this);
		SurveySubsystem->OnSurveyResultReceived.RemoveAll(this);
	}
	RestoreView();
	Super::NativeDestruct();
}

void USWGHoloSurveyWidget::Project()
{
	APawn* Pawn = GetOwningPlayerPawn();
	UWorld* World = GetWorld();
	if (!Pawn || !World || Hologram)
	{
		return;
	}
	const FRotator Facing(0.f, Pawn->GetActorRotation().Yaw, 0.f);
	FVector ProjectorLocation;
	SWGHoloProjector::FindLocation(Pawn, ProjectorDistance, ProjectorHeight, ProjectorLocation);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Hologram = World->SpawnActor<ASWGHoloSurveyActor>(ASWGHoloSurveyActor::StaticClass(), ProjectorLocation, FRotator::ZeroRotator, Params);
	if (!Hologram)
	{
		return;
	}
	Hologram->SetDroidSide(FVector2D(Facing.RotateVector(FVector(0.f, 1.f, 0.f))));
	const FVector CameraLocation = ProjectorLocation + Facing.RotateVector(CameraOffset);
	View.Begin(*this, CameraLocation, Hologram->GetFocusLocation(), 70.f, CameraBlendSeconds, HudOpacity);
}

void USWGHoloSurveyWidget::RestoreView()
{
	View.End(*this);
	if (bExploring)
	{
		if (APlayerController* PlayerController = GetOwningPlayer())
		{
			PlayerController->SetInputMode(FInputModeGameOnly());
		}
	}
	const TArray<FVector> EdgeTargets = GetFieldEdgeTargets();
	if (Hologram)
	{
		if (bExploring && Field && Field->HasField())
		{
			Hologram->CollapseField(Field->GetFieldCenter(), EdgeTargets, CollapseSeconds);
		}
		else
		{
			Hologram->Destroy();
		}
		Hologram = nullptr;
	}
	if (Field)
	{
		Field->FadeOutAndDestroy(CollapseSeconds);
		Field = nullptr;
	}
}

void USWGHoloSurveyWidget::Close()
{
	RestoreView();
	RemoveFromParent();
	OnClosed.Broadcast();
}

void USWGHoloSurveyWidget::SwitchToWindow()
{
	OnSwitchToWindow.Broadcast();
}

void USWGHoloSurveyWidget::ShowPicker()
{
	APawn* Pawn = GetOwningPlayerPawn();
	if (!Hologram || !Pawn || !bExploring)
	{
		return;
	}
	bExploring = false;
	RefreshStatus();
	if (Field)
	{
		Field->FadeOutAndDestroy(CollapseSeconds);
		Field = nullptr;
	}
	Hologram->SetFieldMode(false);
	Hologram->SetExtraRays({}, {});
	Hologram->SetExtraRayRadius(Hologram->DiscDiameter * 1.5f);
	const FRotator Facing(0.f, Pawn->GetActorRotation().Yaw, 0.f);
	FVector ProjectorLocation;
	SWGHoloProjector::FindLocation(Pawn, ProjectorDistance, ProjectorHeight, ProjectorLocation);
	Hologram->SetActorLocation(ProjectorLocation);
	Hologram->SetDroidSide(FVector2D(Facing.RotateVector(FVector(0.f, 1.f, 0.f))));
	const FVector CameraLocation = ProjectorLocation + Facing.RotateVector(CameraOffset);
	View.Begin(*this, CameraLocation, Hologram->GetFocusLocation(), 70.f, CameraBlendSeconds, HudOpacity);
	if (StatusText) { StatusText->SetVisibility(ESlateVisibility::Visible); }
	if (HintText) { HintText->SetVisibility(ESlateVisibility::Visible); }
	if (SurveyButton) { SurveyButton->SetVisibility(ESlateVisibility::Visible); }
	if (SampleButton) { SampleButton->SetVisibility(ESlateVisibility::Visible); }
	if (WindowButton) { WindowButton->SetVisibility(ESlateVisibility::Visible); }
	PlaceButtonBar(SurveyButton, false);
	SetRenderOpacity(1.f);
	SetVisibility(ESlateVisibility::Visible);
	SetIsFocusable(true);
	SetFocus();
}

void USWGHoloSurveyWidget::SetSuspended(bool bInSuspended)
{
	if (bSuspended == bInSuspended)
	{
		return;
	}
	bSuspended = bInSuspended;
	APlayerController* PlayerController = GetOwningPlayer();
	if (bSuspended)
	{
		// The picker's camera would fight the map's; a scan has none and stays in the world.
		if (!bExploring)
		{
			View.End(*this);
			if (Hologram) { Hologram->SetActorHiddenInGame(true); }
		}
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (bExploring)
	{
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (PlayerController)
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetWidgetToFocus(TakeWidget());
			PlayerController->SetInputMode(InputMode);
		}
		return;
	}
	APawn* Pawn = GetOwningPlayerPawn();
	if (Hologram && Pawn)
	{
		Hologram->SetActorHiddenInGame(false);
		const FRotator Facing(0.f, Pawn->GetActorRotation().Yaw, 0.f);
		const FVector CameraLocation = Hologram->GetActorLocation() + Facing.RotateVector(CameraOffset);
		View.Begin(*this, CameraLocation, Hologram->GetFocusLocation(), 70.f, CameraBlendSeconds, HudOpacity);
	}
	SetVisibility(ESlateVisibility::Visible);
	SetFocus();
	if (PlayerController)
	{
		SetUserFocus(PlayerController);
	}
}

void USWGHoloSurveyWidget::RefreshEntries()
{
	if (!Hologram || !SurveySubsystem)
	{
		return;
	}
	PickerResources = SurveySubsystem->GetResources();
	PickerResources.Sort([](const FSWGSurveyResource& Left, const FSWGSurveyResource& Right)
	{
		const FString LeftPath = FString::Join(Left.ClassPath, TEXT("/")) + TEXT("/") + Left.ClassName;
		const FString RightPath = FString::Join(Right.ClassPath, TEXT("/")) + TEXT("/") + Right.ClassName;
		const int32 PathOrder = LeftPath.Compare(RightPath, ESearchCase::IgnoreCase);
		return PathOrder == 0 ? Left.Name.Compare(Right.Name, ESearchCase::IgnoreCase) < 0 : PathOrder < 0;
	});
	PickerPath.Reset();
	PickerHistory.Reset();
	BuildPickerEntries();
}

void USWGHoloSurveyWidget::BuildPickerEntries()
{
	if (!Hologram || !SurveySubsystem)
	{
		return;
	}
	TArray<const FSWGSurveyResource*> Matches;
	for (const FSWGSurveyResource& Resource : PickerResources)
	{
		TArray<FString> Branch = Resource.ClassPath;
		Branch.Add(Resource.ClassName);
		bool bMatches = PickerPath.Num() <= Branch.Num();
		for (int32 Index = 0; bMatches && Index < PickerPath.Num(); ++Index)
		{
			bMatches = Branch[Index].Equals(PickerPath[Index], ESearchCase::IgnoreCase);
		}
		if (bMatches)
		{
			Matches.Add(&Resource);
		}
	}
	// Single-child levels don't require a choice. Stop once paths branch or names are next.
	while (!Matches.IsEmpty())
	{
		TSet<FString> Children;
		bool bHasNamedResource = false;
		for (const FSWGSurveyResource* Resource : Matches)
		{
			const int32 Depth = PickerPath.Num();
			if (Depth < Resource->ClassPath.Num()) { Children.Add(Resource->ClassPath[Depth]); }
			else if (Depth == Resource->ClassPath.Num()) { Children.Add(Resource->ClassName); }
			else { bHasNamedResource = true; }
		}
		if (Children.Num() != 1 || bHasNamedResource) { break; }
		PickerPath.Add(*Children.CreateConstIterator());
	}
	TArray<ASWGHoloSurveyActor::FEntry> Entries;
	EntryNames.Reset();
	EntryBranches.Reset();
	TSet<FString> Children;
	for (const FSWGSurveyResource* Resource : Matches)
	{
		const int32 Depth = PickerPath.Num();
		const FString Child = Depth < Resource->ClassPath.Num() ? Resource->ClassPath[Depth]
			: Depth == Resource->ClassPath.Num() ? Resource->ClassName : FString();
		if (Child.IsEmpty())
		{
			Entries.Add({ Resource->Name, Resource->ClassName });
			EntryNames.Add(Resource->Name);
			EntryBranches.Add(FString());
		}
		else if (!Children.Contains(Child))
		{
			Children.Add(Child);
			Entries.Add({ Child + TEXT("  >"), PickerHistory.IsEmpty() ? TEXT("Type") : TEXT("Subtype") });
			EntryNames.Add(FString());
			EntryBranches.Add(Child);
		}
	}
	int32 Selected = EntryNames.IndexOfByKey(SurveySubsystem->GetSelectedResource());
	if (Selected == INDEX_NONE) { Selected = 0; }
	Hologram->SetEntries(Entries);
	Hologram->SetHeading(PickerPath.IsEmpty() ? SurveySubsystem->GetSurveyTypeDisplayName()
		: FText::FromString(PickerPath.Last()));
	Hologram->SetSelectedIndex(Selected);
	if (EntryNames.IsValidIndex(Selected) && !EntryNames[Selected].IsEmpty())
	{
		SurveySubsystem->SetSelectedResource(EntryNames[Selected]);
	}
	RefreshStatus();
}

void USWGHoloSurveyWidget::RefreshStatus()
{
	if (!StatusText || !SurveySubsystem)
	{
		return;
	}
	const FText Heading = SurveySubsystem->GetSurveyTypeDisplayName();
	FText Line;
	if (SurveySubsystem->IsSurveyPending())
	{
		Line = FText::Format(NSLOCTEXT("SWGEmu", "HoloSurveyPending", "Surveying for {0}..."), FText::FromString(SurveySubsystem->GetSelectedResource()));
	}
	else if (!TransientStatus.IsEmpty())
	{
		Line = TransientStatus;
	}
	else if (!bExploring)
	{
		Line = EntryNames.Contains(FString())
			? (PickerHistory.IsEmpty()
				? NSLOCTEXT("SWGEmu", "HoloSurveyPickType", "Choose a resource type")
				: NSLOCTEXT("SWGEmu", "HoloSurveyPickSubtype", "Choose a subtype"))
			: NSLOCTEXT("SWGEmu", "HoloSurveyPick", "Pick a resource to survey for");
	}
	else if (SurveySubsystem->HasResult())
	{
		const FSWGSurveyResult& Result = SurveySubsystem->GetLastResult();
		const float Best = Result.GetBest().Density;
		Line = Best >= FSWGSurveyResult::WaypointDensity
			? FText::Format(NSLOCTEXT("SWGEmu", "HoloSurveyBest", "{0}: highest concentration {1} — waypoint set"),
				FText::FromString(Result.ResourceName), SWGSurveyStyle::DensityText(Best))
			: FText::Format(NSLOCTEXT("SWGEmu", "HoloSurveyNothing", "{0}: nothing worth a waypoint here"), FText::FromString(Result.ResourceName));
	}
	else { Line = FText::GetEmpty(); }
	StatusText->SetText(FText::Format(NSLOCTEXT("SWGEmu", "HoloSurveyStatus", "{0}\n{1}"), Heading, Line));
}

void USWGHoloSurveyWidget::ShowField()
{
	UWorld* World = GetWorld();
	if (!World || !SurveySubsystem || !SurveySubsystem->HasResult())
	{
		return;
	}
	if (Field)
	{
		Field->Destroy();
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Field = World->SpawnActor<ASWGHoloSurveyFieldActor>(ASWGHoloSurveyFieldActor::StaticClass(), FTransform::Identity, Params);
	if (Field)
	{
		Field->SetResult(SurveySubsystem->GetLastResult());
		bExploring = true;
		if (Hologram)
		{
			Hologram->SetFieldMode(true);
		}
		View.End(*this);
		if (StatusText) { StatusText->SetVisibility(ESlateVisibility::Collapsed); }
		if (HintText) { HintText->SetVisibility(ESlateVisibility::Collapsed); }
		if (SurveyButton) { SurveyButton->SetVisibility(ESlateVisibility::Visible); }
		if (SampleButton) { SampleButton->SetVisibility(ESlateVisibility::Visible); }
		if (WindowButton) { WindowButton->SetVisibility(ESlateVisibility::Collapsed); }
		PlaceButtonBar(SurveyButton, true);
		if (bSuspended)
		{
			// A scan that lands under the map stays out of its way; SetSuspended(false) brings the controls back.
			if (Hologram) { Hologram->SetActorHiddenInGame(false); }
			SetVisibility(ESlateVisibility::Collapsed);
			return;
		}
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (APlayerController* PlayerController = GetOwningPlayer())
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetWidgetToFocus(TakeWidget());
			PlayerController->SetInputMode(InputMode);
		}
	}
}

void USWGHoloSurveyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bExploring && Hologram && Field && Field->HasField())
	{
		const APlayerController* PlayerController = GetOwningPlayer();
		const FVector Facing = PlayerController ? PlayerController->GetControlRotation().Vector().GetSafeNormal2D() : FVector::ForwardVector;
		const float ForwardDistance = FMath::Min(ScanDroidForwardDistance, Field->GetFieldHalfExtent() * 0.8f);
		Hologram->SetActorLocation(Field->GetFieldCenter() + Facing * ForwardDistance + FVector(0.f, 0.f, ScanDroidHeight));
	}
	if (Hologram && SurveySubsystem)
	{
		Hologram->SetScanning(SurveySubsystem->IsSurveyPending());
	}
	const bool bHasEntry = Hologram && EntryNames.IsValidIndex(Hologram->GetSelectedIndex());
	const bool bNamedResource = bHasEntry && !EntryNames[Hologram->GetSelectedIndex()].IsEmpty();
	if (SurveyButton) { SurveyButton->SetIsEnabled(SurveySubsystem && !SurveySubsystem->IsSurveyPending()
		&& (bExploring ? !SurveySubsystem->GetSelectedResource().IsEmpty() : bHasEntry)); }
	// While scanning the sample is of the resource surveyed for, whatever the picker last showed.
	if (SampleButton) { SampleButton->SetIsEnabled(bExploring ? SurveySubsystem && !SurveySubsystem->GetSelectedResource().IsEmpty() : bNamedResource); }
	if (SurveyButton)
	{
		if (UTextBlock* Label = Cast<UTextBlock>(SurveyButton->GetChildAt(0)))
		{
			Label->SetText(bExploring ? NSLOCTEXT("SWGEmu", "HoloSurveyScan", "Scan")
				: bNamedResource ? NSLOCTEXT("SWGEmu", "HoloSurveySurvey", "Survey")
				: NSLOCTEXT("SWGEmu", "HoloSurveyOpen", "Open"));
		}
	}
	UpdateDroidRays();
}

void USWGHoloSurveyWidget::UpdateDroidRays()
{
	if (!Hologram)
	{
		return;
	}
	TArray<FVector> Targets;
	TArray<float> Brightness;
	if (bExploring && Field && Field->HasField())
	{
		Targets = GetFieldEdgeTargets();
		Brightness.Init(0.2f, Targets.Num());
		Hologram->SetExtraRayRadius(Field->GetFieldHalfExtent() * 3.f);
	}
	else if (!bExploring)
	{
		const int32 SelectedIndex = Hologram->GetSelectedIndex();
		for (int32 Offset : { -2, 0, 2 })
		{
			FVector Location;
			if (Hologram->GetEntryLocation(SelectedIndex + Offset, Location))
			{
				Targets.Add(Location);
				Brightness.Add(Offset == 0 ? 0.16f : 0.06f);
			}
		}
	}
	Hologram->SetExtraRays(Targets, Brightness);
}

TArray<FVector> USWGHoloSurveyWidget::GetFieldEdgeTargets() const
{
	TArray<FVector> Targets;
	if (!Field || !Field->HasField())
	{
		return Targets;
	}
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const FVector ThisCorner = Field->GetCornerLocation(Corner);
		Targets.Add(ThisCorner);
		Targets.Add((ThisCorner + Field->GetCornerLocation((Corner + 1) % 4)) * 0.5f);
	}
	return Targets;
}

void USWGHoloSurveyWidget::SelectNext(int32 Direction)
{
	if (!Hologram || !SurveySubsystem || EntryNames.IsEmpty())
	{
		return;
	}
	const int32 Index = FMath::Clamp(Hologram->GetSelectedIndex() + Direction, 0, EntryNames.Num() - 1);
	Hologram->SetSelectedIndex(Index);
	if (!EntryNames[Index].IsEmpty()) { SurveySubsystem->SetSelectedResource(EntryNames[Index]); }
}

void USWGHoloSurveyWidget::Survey()
{
	if (!Hologram || !SurveySubsystem) { return; }
	if (bExploring)
	{
		SurveySubsystem->RequestSurvey();
		return;
	}
	const int32 Index = Hologram->GetSelectedIndex();
	if (EntryBranches.IsValidIndex(Index) && !EntryBranches[Index].IsEmpty())
	{
		PickerHistory.Add(PickerPath.Num());
		PickerPath.Add(EntryBranches[Index]);
		BuildPickerEntries();
		return;
	}
	if (EntryNames.IsValidIndex(Index) && !EntryNames[Index].IsEmpty())
	{
		SurveySubsystem->SetSelectedResource(EntryNames[Index]);
		SurveySubsystem->RequestSurvey();
	}
}

void USWGHoloSurveyWidget::Sample()
{
	const int32 Index = Hologram ? Hologram->GetSelectedIndex() : INDEX_NONE;
	const bool bHasResource = bExploring || (EntryNames.IsValidIndex(Index) && !EntryNames[Index].IsEmpty());
	if (SurveySubsystem && bHasResource && SurveySubsystem->RequestSample())
	{
		TransientStatus = FText::Format(NSLOCTEXT("SWGEmu", "HoloSurveySampling", "Sampling for {0}..."), FText::FromString(SurveySubsystem->GetSelectedResource()));
		RefreshStatus();
	}
}

void USWGHoloSurveyWidget::BackPicker()
{
	if (PickerHistory.IsEmpty()) { Close(); return; }
	PickerPath.SetNum(PickerHistory.Pop());
	BuildPickerEntries();
}

int32 USWGHoloSurveyWidget::EntryAt(const FVector2D& ViewportPosition) const
{
	const APlayerController* PlayerController = GetOwningPlayer();
	if (!Hologram || !PlayerController)
	{
		return INDEX_NONE;
	}
	int32 Best = INDEX_NONE;
	float BestDistance = PickRadiusPixels;
	for (int32 Index = 0; Index < EntryNames.Num(); ++Index)
	{
		FVector World;
		FVector2D Screen;
		if (Hologram->GetEntryLocation(Index, World) && PlayerController->ProjectWorldLocationToScreen(World, Screen))
		{
			const float Distance = FVector2D::Distance(Screen, ViewportPosition);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Index;
			}
		}
	}
	return Best;
}

FReply USWGHoloSurveyWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	SelectNext(InMouseEvent.GetWheelDelta() > 0.f ? -1 : 1);
	return FReply::Handled();
}

FReply USWGHoloSurveyWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && Hologram)
	{
		FVector2D PixelPosition;
		FVector2D ViewportPosition;
		USlateBlueprintLibrary::AbsoluteToViewport(this, InMouseEvent.GetScreenSpacePosition(), PixelPosition, ViewportPosition);
		const int32 Index = EntryAt(PixelPosition);
		if (Index != INDEX_NONE)
		{
			SelectNext(Index - Hologram->GetSelectedIndex());
		}
	}
	return FReply::Handled().SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

FReply USWGHoloSurveyWidget::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	FVector2D PixelPosition;
	FVector2D ViewportPosition;
	USlateBlueprintLibrary::AbsoluteToViewport(this, InMouseEvent.GetScreenSpacePosition(), PixelPosition, ViewportPosition);
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && Hologram && EntryAt(PixelPosition) == Hologram->GetSelectedIndex())
	{
		Survey();
	}
	return FReply::Handled();
}

FReply USWGHoloSurveyWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (bExploring)
	{
		if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
		{
			Survey();
			return FReply::Handled();
		}
		if (Key == EKeys::Q || Key == EKeys::Gamepad_FaceButton_Left)
		{
			Sample();
			return FReply::Handled();
		}
		if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
		{
			Close();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
	{
		BackPicker();
		return FReply::Handled();
	}
	if (Key == EKeys::BackSpace || Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left)
	{
		BackPicker();
		return FReply::Handled();
	}
	if (Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up)
	{
		SelectNext(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down)
	{
		SelectNext(1);
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		Survey();
		return FReply::Handled();
	}
	if (Key == EKeys::Q || Key == EKeys::Gamepad_FaceButton_Left)
	{
		Sample();
		return FReply::Handled();
	}
	if (Key == EKeys::Tab || Key == EKeys::Gamepad_FaceButton_Top)
	{
		SwitchToWindow();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void USWGHoloSurveyWidget::HandleResourcesChanged()
{
	RefreshEntries();
	RefreshStatus();
}

void USWGHoloSurveyWidget::HandleSurveyStateChanged()
{
	TransientStatus = FText::GetEmpty();
	if (Hologram && SurveySubsystem)
	{
		const int32 Index = EntryNames.IndexOfByKey(SurveySubsystem->GetSelectedResource());
		if (Index != INDEX_NONE)
		{
			Hologram->SetSelectedIndex(Index);
		}
	}
	RefreshStatus();
}

void USWGHoloSurveyWidget::HandleResultReceived()
{
	ShowField();
	RefreshStatus();
}

void USWGHoloSurveyWidget::HandleSurveyClicked() { Survey(); }
void USWGHoloSurveyWidget::HandleSampleClicked() { Sample(); }
void USWGHoloSurveyWidget::HandleWindowClicked() { SwitchToWindow(); }
void USWGHoloSurveyWidget::HandleCloseClicked() { Close(); }
