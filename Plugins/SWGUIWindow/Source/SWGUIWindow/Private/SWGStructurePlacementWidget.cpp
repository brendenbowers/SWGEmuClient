#include "SWGStructurePlacementWidget.h"
#include "SWGPlacementViewMode.h"
#include "Subsystems/SWGStructurePlacementSubsystem.h"
#include "Subsystems/SWGTerrainSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "Common/SWGWorldScale.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraActor.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"

namespace
{
	constexpr float PlacementPitchDegrees = 75.f, PlacementFovDegrees = 70.f, PlacementRangeMeters = 100.f;

	/** Camera height (cm) at which the server's placement range circle, plus a 10% margin, just fits the viewport's vertical span. */
	float MaxPlacementHeight(const APlayerController* PC)
	{
		int32 Width = 0, Height = 0;
		if (PC) { PC->GetViewportSize(Width, Height); }
		const float Aspect = Width > 0 && Height > 0 ? static_cast<float>(Width) / Height : 16.f / 9.f;
		const float HalfVertical = FMath::Atan(FMath::Tan(FMath::DegreesToRadians(PlacementFovDegrees * .5f)) / Aspect);
		const float Pitch = FMath::DegreesToRadians(PlacementPitchDegrees);
		const float SpanPerHeight = 1.f / FMath::Tan(Pitch - HalfVertical) - 1.f / FMath::Tan(Pitch + HalfVertical);
		return PlacementRangeMeters * 2.f * 1.1f * 100.f / SpanPerHeight;
	}
}

void USWGStructurePlacementWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	// Canvas panels are not hit-testable by default; without this, clicks on empty screen never reach the mouse handlers below.
	Canvas->SetVisibility(ESlateVisibility::Visible);
	WidgetTree->RootWidget = Canvas;
	PlacementPanel = WidgetTree->ConstructWidget<UVerticalBox>();
	UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(PlacementPanel);
	PanelSlot->SetAnchors(FAnchors(0.f, 0.f));
	PanelSlot->SetAlignment(FVector2D::ZeroVector);
	PanelSlot->SetAutoSize(true);
	PanelSlot->SetPosition(FVector2D(12.f, 12.f));
	StatusText = WidgetTree->ConstructWidget<UTextBlock>();
	StatusText->SetText(FText::FromString(TEXT("Structure placement")));
	StatusText->SetWrapTextAt(880.f);
	PlacementPanel->AddChildToVerticalBox(StatusText);
	PromptBorder = WidgetTree->ConstructWidget<UBorder>();
	PromptBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, .7f));
	PromptBorder->SetPadding(FMargin(4.f, 2.f));
	PromptBorder->SetVisibility(ESlateVisibility::Collapsed);
	PlacementPanel->AddChildToVerticalBox(PromptBorder);
	PromptText = WidgetTree->ConstructWidget<UTextBlock>();
	FSlateFontInfo PromptFont = StatusText->GetFont();
	PromptFont.Size = 13;
	PromptText->SetFont(PromptFont);
	PromptBorder->AddChild(PromptText);
}

void USWGStructurePlacementWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Placement = GetGameInstance()->GetSubsystem<USWGStructurePlacementSubsystem>();
	Terrain = GetGameInstance()->GetSubsystem<USWGTerrainSubsystem>();
	if (!Placement || !Placement->IsActive() || !Terrain || !GetOwningPlayerPawn()) { Close(); return; }
	DisplayName = FPaths::GetBaseFilename(Placement->GetTemplatePath()).Replace(TEXT("shared_"), TEXT(""));
	if (USWGTreSubsystem* Tre = GetGameInstance()->GetSubsystem<USWGTreSubsystem>())
	{
		FString Table, Key;
		if (Tre->FindTemplateStringId(Placement->GetTemplatePath(), TEXT("objectName"), Table, Key))
		{
			const FString Name = Tre->LookupString(Table, Key);
			if (!Name.IsEmpty()) { DisplayName = Name; }
		}
	}
	Placement->OnPlacementUpdated.AddUniqueDynamic(this, &USWGStructurePlacementWidget::Refresh);
	Placement->OnPlacementEnded.AddUniqueDynamic(this, &USWGStructurePlacementWidget::HandleEnded);
	ToggleRequestedHandle = FSWGPlacementViewRegistry::OnToggleRequested().AddUObject(this, &USWGStructurePlacementWidget::ToggleMode);
	CommandRequestedHandle = FSWGPlacementViewRegistry::OnCommandRequested().AddUObject(this, &USWGStructurePlacementWidget::HandleModeCommand);
	PlayerOrigin = GetOwningPlayerPawn()->GetActorLocation();
	const FSWGStructureFootprint& Footprint = Placement->GetFootprint();
	BaseHeight = FMath::Clamp(FMath::Max(Footprint.Cols * Footprint.ColChunkSize, Footprint.Rows * Footprint.RowChunkSize) * 500.f, 8000.f, 32000.f);
	Focus = PlayerOrigin + GetOwningPlayerPawn()->GetActorForwardVector() * Footprint.Rows * Footprint.RowChunkSize * 50.f;
	const FVector RawFocus = SWGToRawSpace(Focus);
	Focus.Z = Terrain->GetHeightAt(RawFocus.X, RawFocus.Y) * SWGWorldScale;
	Retarget();
	if (APlayerController* PC = GetOwningPlayer())
	{
		bPreviousCursor = PC->bShowMouseCursor;
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		SetUserFocus(PC);
	}
	Refresh();
}

void USWGStructurePlacementWidget::NativeDestruct()
{
	if (Placement) { Placement->OnPlacementUpdated.RemoveAll(this); Placement->OnPlacementEnded.RemoveAll(this); }
	FSWGPlacementViewRegistry::OnToggleRequested().Remove(ToggleRequestedHandle);
	FSWGPlacementViewRegistry::OnCommandRequested().Remove(CommandRequestedHandle);
	for (const TPair<FName, TObjectPtr<USWGPlacementViewMode>>& Entry : Modes)
	{
		if (Entry.Value)
		{
			Entry.Value->OnStateChanged.RemoveAll(this);
			if (Entry.Value->IsActive()) { Entry.Value->End(); }
		}
	}
	ActiveMode = nullptr;
	View.End(*this);
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->bShowMouseCursor = bPreviousCursor;
		FInputModeGameAndUI Mode;
		PC->SetInputMode(Mode);
	}
	Super::NativeDestruct();
}

void USWGStructurePlacementWidget::Close() { RemoveFromParent(); }
void USWGStructurePlacementWidget::HandleEnded() { Close(); }
void USWGStructurePlacementWidget::Cancel() { if (Placement) { Placement->Cancel(); } }
void USWGStructurePlacementWidget::Rotate() { if (Placement) { Placement->Rotate(1); } }
void USWGStructurePlacementWidget::Place() { if (Placement && Placement->GetValidation().Verdict == ESWGPlacementVerdict::Valid) { Placement->Confirm(); } else { Refresh(); } }

bool USWGStructurePlacementWidget::IsModeActive() const
{
	return ActiveMode && ActiveMode->IsActive();
}

bool USWGStructurePlacementWidget::ActiveModePassesInputToWorld() const
{
	return IsModeActive() && ActiveMode->PassesInputToWorld();
}

void USWGStructurePlacementWidget::ToggleMode(FName ModeId)
{
	const FSWGPlacementViewRegistration* Registration = FSWGPlacementViewRegistry::GetAll().FindByPredicate(
		[ModeId](const FSWGPlacementViewRegistration& Candidate) { return Candidate.Id == ModeId; });
	if (!Registration || !Registration->ModeClass) { return; }

	TObjectPtr<USWGPlacementViewMode>& ModeSlot = Modes.FindOrAdd(ModeId);
	if (!ModeSlot)
	{
		ModeSlot = NewObject<USWGPlacementViewMode>(this, Registration->ModeClass);
		ModeSlot->OnStateChanged.AddUObject(this, &USWGStructurePlacementWidget::HandleModeStateChanged);
	}
	USWGPlacementViewMode* Mode = ModeSlot;

	// Toggling the active view returns to the overhead one; toggling another swaps.
	const bool bWasThisMode = ActiveMode == Mode && Mode->IsActive();
	if (IsModeActive()) { ActiveMode->End(); }
	ActiveMode = nullptr;
	if (bWasThisMode) { Retarget(); Refresh(); return; }
	if (Mode->Begin(this, &View)) { ActiveMode = Mode; }
	else { Retarget(); }
	Refresh();
}

void USWGStructurePlacementWidget::HandleModeCommand(FName ModeId, FName Command)
{
	if (TObjectPtr<USWGPlacementViewMode>* Mode = Modes.Find(ModeId); Mode && *Mode && (*Mode)->IsActive())
	{
		(*Mode)->HandleCommand(Command);
	}
}

void USWGStructurePlacementWidget::HandleModeStateChanged()
{
	// A mode that ended by itself (Esc on its first screen) hands the camera back to the overhead view.
	if (ActiveMode && !ActiveMode->IsActive())
	{
		ActiveMode = nullptr;
		Retarget();
	}
	Refresh();
	if (APlayerController* PC = GetOwningPlayer()) { SetUserFocus(PC); }
}

void USWGStructurePlacementWidget::Refresh()
{
	if (!Placement || !StatusText) { return; }
	const FSWGPlacementValidation Validation = Placement->GetValidation();
	const TCHAR* State = Validation.Verdict == ESWGPlacementVerdict::Valid ? TEXT("Ready") : Validation.Verdict == ESWGPlacementVerdict::Uncertain ? TEXT("Server will check") : TEXT("Cannot place");
	const FString Detail = IsModeActive() ? ActiveMode->GetStatusDetail() : FString();
	StatusText->SetText(FText::FromString(Detail.IsEmpty()
		? FString::Printf(TEXT("%s  |  %s"), *DisplayName, State)
		: FString::Printf(TEXT("%s  |  %s  |  %s"), *DisplayName, *Detail, State)));
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(.75f, .9f, 1.f)));
	if (PromptText)
	{
		const FString Prompt = IsModeActive() ? ActiveMode->GetPrompt() : FString();
		PromptBorder->SetVisibility(Prompt.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		PromptText->SetText(FText::FromString(Prompt));
	}
}

void USWGStructurePlacementWidget::Retarget()
{
	const float Height = FMath::Min(BaseHeight * Zoom, MaxPlacementHeight(GetOwningPlayer()));
	const FVector Forward = GetOwningPlayerPawn() ? GetOwningPlayerPawn()->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	const FVector Camera = Focus - Forward * (Height / FMath::Tan(FMath::DegreesToRadians(75.f))) + FVector(0.f, 0.f, Height);
	if (!View.GetCamera()) { View.Begin(*this, Camera, Focus, 70.f, 0.35f, 0.f); }
	else { View.Retarget(Camera, Focus, 70.f); }
}

void USWGStructurePlacementWidget::Pan(FVector2D Direction, float DeltaTime)
{
	const FVector Forward = GetOwningPlayerPawn() ? GetOwningPlayerPawn()->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	FVector Next = Focus + (Forward * Direction.Y + Right * Direction.X) * 2500.f * FMath::Max(1.f, Zoom) * DeltaTime;
	const FVector Delta = (Next - PlayerOrigin).GetClampedToMaxSize2D(10000.f);
	Focus = PlayerOrigin + Delta;
	Focus.Z = SWGToUnrealSpace(FVector(SWGToRawSpace(Focus).X, SWGToRawSpace(Focus).Y, Terrain->GetHeightAt(SWGToRawSpace(Focus).X, SWGToRawSpace(Focus).Y))).Z;
	Retarget();
}

bool USWGStructurePlacementWidget::CursorToTerrain(FVector2D& OutRaw) const
{
	if (!Terrain || !Terrain->GetPlanetData()) { return false; }
	APlayerController* PC = GetOwningPlayer();
	float X, Y;
	FVector Origin, Direction;
	if (!PC || !PC->GetMousePosition(X, Y) || !PC->DeprojectScreenPositionToWorld(X, Y, Origin, Direction)) { return false; }
	// Trace the height field itself so buildings and creatures cannot steal the cursor hit.
	float PreviousT = 0.f;
	float PreviousDelta = SWGToRawSpace(Origin).Z - Terrain->GetHeightAt(SWGToRawSpace(Origin).X, SWGToRawSpace(Origin).Y);
	for (int32 Index = 1; Index <= 48; ++Index)
	{
		const float T = Index * 2500.f;
		const FVector Raw = SWGToRawSpace(Origin + Direction * T);
		const float Delta = Raw.Z - Terrain->GetHeightAt(Raw.X, Raw.Y);
		if (PreviousDelta >= 0.f && Delta <= 0.f)
		{
			float Low = PreviousT, High = T;
			for (int32 Step = 0; Step < 10; ++Step)
			{
				const float Mid = (Low + High) * .5f;
				const FVector At = SWGToRawSpace(Origin + Direction * Mid);
				if (At.Z > Terrain->GetHeightAt(At.X, At.Y)) { Low = Mid; } else { High = Mid; }
			}
			const FVector Hit = SWGToRawSpace(Origin + Direction * ((Low + High) * .5f));
			OutRaw = FVector2D(Hit.X, Hit.Y);
			return true;
		}
		PreviousT = T;
		PreviousDelta = Delta;
	}
	return false;
}

void USWGStructurePlacementWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (!Placement || !Placement->IsActive()) { Close(); return; }
	ViewAge += DeltaTime;
	APlayerController* PC = GetOwningPlayer();
	if (!PC || (ViewAge > .5f && View.GetCamera() && PC->GetViewTarget() != View.GetCamera())) { Placement->Cancel(); return; }
	if (Terrain && Terrain->GetPlanetData() && GetOwningPlayerPawn())
	{
		const FVector PawnLocation = GetOwningPlayerPawn()->GetActorLocation();
		if (FMath::Abs(PawnLocation.Z - PlayerOrigin.Z) > 1000.f)
		{
			Focus += PawnLocation - PlayerOrigin;
			PlayerOrigin = PawnLocation;
			if (!IsModeActive()) { Retarget(); }
		}
	}
	if (IsModeActive()) { ActiveMode->Tick(DeltaTime); return; }
	FVector2D Move(0.f, 0.f);
	if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up)) { Move.Y += 1.f; }
	if (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down)) { Move.Y -= 1.f; }
	if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right)) { Move.X += 1.f; }
	if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left)) { Move.X -= 1.f; }
	float MouseX, MouseY;
	int32 Width, Height;
	PC->GetViewportSize(Width, Height);
	if (PC->GetMousePosition(MouseX, MouseY))
	{
		const float EdgeX = FMath::Max(24.f, Width * .03f), EdgeY = FMath::Max(24.f, Height * .03f);
		if (MouseX < EdgeX) { Move.X -= 1.f; } else if (MouseX > Width - EdgeX) { Move.X += 1.f; }
		if (MouseY < EdgeY) { Move.Y += 1.f; } else if (MouseY > Height - EdgeY) { Move.Y -= 1.f; }
	}
	if (!Move.IsNearlyZero()) { Pan(Move.GetSafeNormal(), DeltaTime); }
	FVector2D Raw;
	if (CursorToTerrain(Raw) && !Raw.Equals(Placement->GetCandidate(), 0.1f)) { Placement->SetCandidate(Raw); }
}

FReply USWGStructurePlacementWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (IsModeActive() && ActiveMode->HandleKeyDown(Event.GetKey(), Event.IsShiftDown(), Event.IsControlDown())) { return FReply::Handled(); }
	if (ActiveModePassesInputToWorld()) { return FReply::Unhandled(); }
	for (const FSWGPlacementViewRegistration& Registration : FSWGPlacementViewRegistry::GetAll())
	{
		if (Event.GetKey() == Registration.ToggleKey) { ToggleMode(Registration.Id); return FReply::Handled(); }
	}
	if (Event.GetKey() == EKeys::Escape) { Cancel(); return FReply::Handled(); }
	if (Event.GetKey() == EKeys::Enter || Event.GetKey() == EKeys::SpaceBar) { Place(); return FReply::Handled(); }
	if (Event.GetKey() == EKeys::R) { Rotate(); return FReply::Handled(); }
	return FReply::Handled();
}

FReply USWGStructurePlacementWidget::NativeOnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (IsModeActive()) { ActiveMode->HandleKeyUp(Event.GetKey()); }
	if (ActiveModePassesInputToWorld()) { return FReply::Unhandled(); }
	return FReply::Handled();
}

void USWGStructurePlacementWidget::NativeOnFocusLost(const FFocusEvent& Event)
{
	for (const TPair<FName, TObjectPtr<USWGPlacementViewMode>>& Entry : Modes)
	{
		if (Entry.Value) { Entry.Value->HandleFocusLost(); }
	}
	Super::NativeOnFocusLost(Event);
}

FReply USWGStructurePlacementWidget::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (IsModeActive() && ActiveMode->HandleMouseWheel(Event.GetWheelDelta())) { return FReply::Handled(); }
	if (ActiveModePassesInputToWorld()) { return FReply::Unhandled(); }
	if (Event.IsShiftDown()) { Rotate(); }
	else { const float MaxZoom = MaxPlacementHeight(GetOwningPlayer()) / BaseHeight;
		Zoom = FMath::Clamp(Zoom * (Event.GetWheelDelta() > 0.f ? .85f : 1.f / .85f), FMath::Min(.5f, MaxZoom), MaxZoom); Retarget(); }
	return FReply::Handled();
}

FReply USWGStructurePlacementWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (IsModeActive() && ActiveMode->HandleMouseButtonDown(Event.GetEffectingButton()))
	{
		// A drag (turning the holo map, moving a droid) keeps the mouse until its button is released.
		return ActiveMode->WantsMouseCapture(Event.GetEffectingButton()) ? FReply::Handled().CaptureMouse(TakeWidget()) : FReply::Handled();
	}
	if (ActiveModePassesInputToWorld()) { return FReply::Unhandled(); }
	if (Event.GetEffectingButton() == EKeys::RightMouseButton) { Rotate(); }
	else if (Event.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		// Toggle between the default height and the furthest zoom-out.
		const float MaxZoom = MaxPlacementHeight(GetOwningPlayer()) / BaseHeight;
		Zoom = Zoom < MaxZoom * .95f ? MaxZoom : FMath::Min(1.f, MaxZoom);
		Retarget();
	}
	else if (Event.GetEffectingButton() == EKeys::LeftMouseButton) { Place(); }
	return FReply::Handled();
}

FReply USWGStructurePlacementWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (IsModeActive() && ActiveMode->HandleMouseButtonUp(Event.GetEffectingButton())) { return FReply::Handled().ReleaseMouseCapture(); }
	if (ActiveModePassesInputToWorld()) { return FReply::Unhandled(); }
	return Super::NativeOnMouseButtonUp(Geometry, Event);
}

FReply USWGStructurePlacementWidget::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (IsModeActive() && ActiveMode->HandleMouseMove(Event.GetCursorDelta())) { return FReply::Handled(); }
	return Super::NativeOnMouseMove(Geometry, Event);
}
