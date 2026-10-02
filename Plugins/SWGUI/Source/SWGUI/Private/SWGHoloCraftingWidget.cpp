#include "SWGHoloCraftingWidget.h"
#include "SWGHoloCraftingActor.h"
#include "SWGHoloStyle.h"
#include "SWGInventoryQuery.h"
#include "SWGRetailStyle.h"
#include "SWGTravelWidget.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Network/Objects/Zone/Object/CraftingDraftSlot.h"
#include "Objects/Tangible/SWGItem.h"
#include "Subsystems/SWGCraftingSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGCrc32.h"
#include "TRE/SWGObjectTemplateReader.h"
#include "TRE/SWGResourceClassRow.h"
#include "TRE/SWGShaderReader.h"
#include "Misc/Paths.h"

namespace
{
	const FLinearColor HintColor = FLinearColor::FromSRGBColor(FColor(0x96, 0xF4, 0xFC));
	// colorReqEmpty/colorOptEmpty/full — the same retail palette
	// (ui_craft_assembly.inc's CodeData) the windowed Assembly page uses.
	const FLinearColor FullColor = FLinearColor::FromSRGBColor(FColor(0x60, 0xFF, 0x60));
	const FLinearColor RequiredEmptyColor = FLinearColor::FromSRGBColor(FColor(0x00, 0xCC, 0x3E));
	const FLinearColor OptionalEmptyColor = FLinearColor::FromSRGBColor(FColor(0xFF, 0x79, 0x00));
	/** Candidates shown per slot's floor row — bounds the mesh-request cost of every slot's row existing at once (not just the focused one). */
	constexpr int32 MaxCandidatesPerRow = 5;
	int32 StageIndex(ESWGCraftingSessionState State)
	{
		switch (State)
		{
			case ESWGCraftingSessionState::Assembling: return 0;
			case ESWGCraftingSessionState::Experimenting: return 1;
			case ESWGCraftingSessionState::Assembled:
			case ESWGCraftingSessionState::Customizing: return 2;
			case ESWGCraftingSessionState::ReadyToFinish: return 3;
			default: return INDEX_NONE;
		}
	}

	UButton* AddButton(UWidgetTree* Tree, UHorizontalBox* Bar, const FText& Caption)
	{
		UButton* Button = Tree->ConstructWidget<UButton>();
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
		Text->SetText(Caption);
		Button->AddChild(Text);
		Bar->AddChildToHorizontalBox(Button)->SetPadding(FMargin(6.f, 0.f));
		return Button;
	}

	/**
	 * Same walk as SWGCraftingWidget.cpp's copy (and SWGResourceClass::
	 * GetIconCandidates) — duplicated rather than shared, matching this
	 * codebase's per-file anonymous-namespace helper convention.
	 */
	bool IsHoloResourceClassOrDescendant(const UDataTable* Table, FString Current, const FString& RequiredType)
	{
		for (int32 Depth = 0; !Current.IsEmpty() && Depth < 16; ++Depth)
		{
			if (Current.Equals(RequiredType, ESearchCase::IgnoreCase))
			{
				return true;
			}
			const FSWGResourceClassRow* Row = Table ? Table->FindRow<FSWGResourceClassRow>(FName(*Current), TEXT("HoloCraft"), false) : nullptr;
			Current = Row ? Row->ParentClass : FString();
		}
		return false;
	}

	/** Component slots carry a shared template path instead of a resource class. */
	bool IsComponentSlot(const FSWGCraftingSlot& SlotEntry)
	{
		return SlotEntry.ResourceType.StartsWith(TEXT("object/"));
	}

	/** Core3's ComponentSlot check: SharedObjectTemplate::isDerivedFrom, i.e. the item's shared template or any DERV ancestor. */
	bool TemplateDerivesFrom(USWGTreSubsystem* Tre, const FString& TemplatePath, const FString& RequiredPath)
	{
		if (!Tre || TemplatePath.IsEmpty()) { return false; }
		// Template data never changes at runtime, so the chains are cached for good.
		static TMap<FString, TArray<FString>> ChainCache;
		TArray<FString>* Chain = ChainCache.Find(TemplatePath);
		if (!Chain)
		{
			Chain = &ChainCache.Add(TemplatePath);
			FString Current = TemplatePath;
			for (int32 Depth = 0; Depth < 16 && !Current.IsEmpty(); ++Depth)
			{
				Chain->Add(Current);
				if (!FSWGObjectTemplateReader::FindDervParentPath(Tre->CreateIffReader(Current), Current)) { break; }
			}
		}
		return Chain->ContainsByPredicate([&RequiredPath](const FString& Path) { return Path.Equals(RequiredPath, ESearchCase::IgnoreCase); });
	}

	FString ResourceClassName(const UDataTable* Table, const FString& Type)
	{
		const FSWGResourceClassRow* Row = Table ? Table->FindRow<FSWGResourceClassRow>(FName(*Type), TEXT("HoloCraft"), false) : nullptr;
		if (Row && !Row->DisplayName.IsEmpty()) { return Row->DisplayName; }
		FString Name = Type.Replace(TEXT("_"), TEXT(" "));
		if (!Name.IsEmpty()) { Name[0] = FChar::ToUpper(Name[0]); }
		return Name;
	}

	/** "Metal › Ferrous Metal › Iron › Doonium Iron" — root first. */
	FString ResourceClassPath(const UDataTable* Table, FString Current)
	{
		TArray<FString> Names;
		for (int32 Depth = 0; !Current.IsEmpty() && Depth < 16; ++Depth)
		{
			Names.Insert(ResourceClassName(Table, Current), 0);
			const FSWGResourceClassRow* Row = Table ? Table->FindRow<FSWGResourceClassRow>(FName(*Current), TEXT("HoloCraft"), false) : nullptr;
			Current = Row ? Row->ParentClass : FString();
		}
		return FString::Join(Names, TEXT(" › "));
	}

	/**
	 * Mirrors Core3's slot checks: ResourceSlot wants the class or a
	 * descendant, ComponentSlot a template DERV'd from the slot's path.
	 * Factory crates never match (their prototype isn't known client-side).
	 */
	bool ItemFitsSlot(USWGTreSubsystem* Tre, const UDataTable* ResourceClasses, USWGObjectGraphSubsystem* ObjectGraph, int64 ObjectId, const FSWGCraftingSlot& SlotEntry)
	{
		if (SlotEntry.ResourceType.IsEmpty()) { return true; }
		const ASWGItem* Item = ObjectGraph ? Cast<ASWGItem>(ObjectGraph->FindActor(ObjectId)) : nullptr;
		if (!Item) { return false; }
		if (IsComponentSlot(SlotEntry))
		{
			return Tre && TemplateDerivesFrom(Tre, Tre->ResolveTemplatePath(Item->SWGObjectCRC), SlotEntry.ResourceType);
		}
		return !Item->ResourceType.IsEmpty() && IsHoloResourceClassOrDescendant(ResourceClasses, Item->ResourceType, SlotEntry.ResourceType);
	}

	/** Hover details for one card row: what the slot accepts and what the bag has for it. */
	UWidget* BuildSlotToolTip(UWidgetTree* Tree, const FSWGCraftingSlot& SlotEntry, const FString& Category, const UDataTable* ResourceClasses, int32 MatchCount)
	{
		TArray<FString> Lines;
		Lines.Add(SlotEntry.Name + (SlotEntry.bOptional ? TEXT(" (optional)") : TEXT("")));
		if (IsComponentSlot(SlotEntry))
		{
			Lines.Add(FString::Printf(TEXT("Component: %s"), *Category));
			Lines.Add(SlotEntry.Kind == ESWGDraftSlotKind::Identical
				? TEXT("All units must be identical (same crafted batch)")
				: TEXT("Units may come from different batches"));
		}
		else if (!SlotEntry.ResourceType.IsEmpty())
		{
			Lines.Add(FString::Printf(TEXT("Resource: %s"), *Category));
			Lines.Add(ResourceClassPath(ResourceClasses, SlotEntry.ResourceType));
		}
		Lines.Add(FString::Printf(TEXT("Needed: %d   Placed: %d"), SlotEntry.RequiredQuantity, SlotEntry.FilledQuantity));
		if (SlotEntry.FilledQuantity > 0)
		{
			Lines.Add(FString::Printf(TEXT("Quality: %d%%"), FMath::RoundToInt(SlotEntry.Quality * 100.f)));
		}
		Lines.Add(MatchCount > 0 ? FString::Printf(TEXT("%d matching item(s) in inventory"), MatchCount) : TEXT("Nothing in inventory fits"));
		UBorder* Panel = Tree->ConstructWidget<UBorder>();
		Panel->SetBrush(SWGHoloStyle::PanelBrush(true));
		Panel->SetPadding(FMargin(10.f, 8.f));
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
		Text->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
		Text->SetFont(SWGHoloStyle::Font(12, false));
		Text->SetColorAndOpacity(FSlateColor(SWGHoloStyle::BrightText));
		Panel->AddChild(Text);
		return Panel;
	}

	/** Just behind and above the character's head, looking down onto the candidate floor. */
	FVector AssemblyCameraLocation(const APawn* Pawn)
	{
		if (!Pawn) { return FVector::ZeroVector; }
		const FRotator Facing(0.f, Pawn->GetActorRotation().Yaw, 0.f);
		return Pawn->GetPawnViewLocation() + Facing.RotateVector(FVector(-30.f, 55.f, 115.f));
	}

	/** What a slot wants, as a short caption: the resource class or the component's object name. */
	FString SlotCategoryName(USWGTreSubsystem* Tre, const UDataTable* Table, const FSWGCraftingSlot& SlotEntry)
	{
		if (!IsComponentSlot(SlotEntry)) { return ResourceClassName(Table, SlotEntry.ResourceType); }
		FString StringTable, StringKey;
		if (Tre && Tre->FindTemplateStringId(SlotEntry.ResourceType, TEXT("objectName"), StringTable, StringKey))
		{
			const FString Name = Tre->LookupString(StringTable, StringKey);
			if (!Name.IsEmpty()) { return Name; }
		}
		FString Name = FPaths::GetBaseFilename(SlotEntry.ResourceType);
		Name.RemoveFromStart(TEXT("shared_"));
		Name.ReplaceInline(TEXT("_"), TEXT(" "));
		if (!Name.IsEmpty()) { Name[0] = FChar::ToUpper(Name[0]); }
		return Name;
	}

	UTexture2D* ResourceTypeIcon(USWGTreSubsystem* Tre, const UDataTable* Table, const FString& Type)
	{
		if (!Tre) { return nullptr; }
		for (const FString& Candidate : SWGResourceClass::GetIconCandidates(Table, Type))
		{
			const FString ShaderPath = FString::Printf(TEXT("shader/ui_res_%s.sht"), *Candidate);
			if (!Tre->FileExists(ShaderPath)) { continue; }
			FSWGShaderData Shader;
			if (FSWGShaderReader::ReadShader(Tre->CreateIffReader(ShaderPath), Shader))
			{
				if (const FSWGShaderTexture* Diffuse = Shader.FindTexture(ESWGShaderTextureUsage::Diffuse))
				{
					return Tre->GetOrLoadTexture(Diffuse->VirtualPath);
				}
			}
		}
		return nullptr;
	}
}

void USWGHoloCraftingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Root;
		StatusText = WidgetTree->ConstructWidget<UTextBlock>();
		StatusText->SetJustification(ETextJustify::Center);
		Root->AddChild(StatusText);
		UCanvasPanelSlot* StatusSlot = Cast<UCanvasPanelSlot>(StatusText->Slot);
		StatusSlot->SetAnchors(FAnchors(0.5f, 0.f));
		StatusSlot->SetAlignment(FVector2D(0.5f, 0.f));
		StatusSlot->SetPosition(FVector2D(0.f, 60.f));
		StatusSlot->SetAutoSize(true);
		SectionList = WidgetTree->ConstructWidget<UVerticalBox>();
		Root->AddChild(SectionList);
		UCanvasPanelSlot* SectionSlot = Cast<UCanvasPanelSlot>(SectionList->Slot);
		SectionSlot->SetAnchors(FAnchors(0.f, 0.5f));
		SectionSlot->SetAlignment(FVector2D(0.f, 0.5f));
		SectionSlot->SetPosition(FVector2D(85.f, 0.f));
		SectionSlot->SetAutoSize(true);

		// The component screen tracks the prototype's screen projection.
		AssemblyCardPanel = WidgetTree->ConstructWidget<UBorder>();
		AssemblyCardPanel->SetBrush(SWGHoloStyle::PanelBrush(true));
		AssemblyCardPanel->SetPadding(FMargin(16.f));
		AssemblyCardPanel->SetVisibility(ESlateVisibility::Collapsed);
		Root->AddChild(AssemblyCardPanel);
		if (UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(AssemblyCardPanel->Slot))
		{
			CardSlot->SetAutoSize(true);
			CardSlot->SetZOrder(10);
		}
		USizeBox* CardWidth = WidgetTree->ConstructWidget<USizeBox>();
		CardWidth->SetWidthOverride(320.f);
		AssemblyCardPanel->AddChild(CardWidth);
		UVerticalBox* CardTextColumn = WidgetTree->ConstructWidget<UVerticalBox>();
		CardWidth->AddChild(CardTextColumn);
		AssemblyCardName = WidgetTree->ConstructWidget<UTextBlock>();
		CardTextColumn->AddChild(AssemblyCardName);
		AssemblySlotList = WidgetTree->ConstructWidget<UVerticalBox>();
		CardTextColumn->AddChild(AssemblySlotList);
		StagePanels.Add(AssemblyCardPanel);
		StageBodies.Add(nullptr);
		for (int32 Stage = 1; Stage <= 3; ++Stage)
		{
			UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
			Panel->SetBrush(SWGHoloStyle::PanelBrush(true));
			Panel->SetPadding(FMargin(16.f));
			Panel->SetVisibility(ESlateVisibility::Collapsed);
			Panel->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			Root->AddChild(Panel);
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Panel->Slot))
			{
				CanvasSlot->SetAutoSize(true);
				CanvasSlot->SetZOrder(10 + Stage);
			}
			USizeBox* Frame = WidgetTree->ConstructWidget<USizeBox>();
			Frame->SetWidthOverride(350.f);
			Frame->SetMaxDesiredHeight(480.f);
			Panel->AddChild(Frame);
			UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
			Frame->AddChild(Scroll);
			UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>();
			Scroll->AddChild(Body);
			StagePanels.Add(Panel);
			StageBodies.Add(Body);
		}

		UHorizontalBox* Bar = WidgetTree->ConstructWidget<UHorizontalBox>();
		Root->AddChild(Bar);
		UCanvasPanelSlot* BarSlot = Cast<UCanvasPanelSlot>(Bar->Slot);
		BarSlot->SetAnchors(FAnchors(0.5f, 1.f));
		BarSlot->SetAlignment(FVector2D(0.5f, 1.f));
		BarSlot->SetPosition(FVector2D(0.f, -40.f));
		BarSlot->SetAutoSize(true);
		HintText = WidgetTree->ConstructWidget<UTextBlock>();
		Bar->AddChildToHorizontalBox(HintText)->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
		CreateButton = AddButton(WidgetTree, Bar, NSLOCTEXT("SWGEmu", "HoloCraftCreate", "Create"));
		TabButton = AddButton(WidgetTree, Bar, NSLOCTEXT("SWGEmu", "HoloCraftSections", "Sections"));
		WindowButton = AddButton(WidgetTree, Bar, NSLOCTEXT("SWGEmu", "HoloCraftWindow", "Window"));
		CloseButton = AddButton(WidgetTree, Bar, NSLOCTEXT("SWGEmu", "HoloCraftClose", "Close"));
	}
	for (UTextBlock* Text : { StatusText.Get(), HintText.Get() })
	{
		if (!Text) { continue; }
		Text->SetFont(SWGRetailStyle::Font(Text == StatusText ? 16 : 13));
		Text->SetColorAndOpacity(FSlateColor(HintColor));
		Text->SetShadowOffset(FVector2D(1.f, 1.f));
	}
	if (AssemblyCardName) { AssemblyCardName->SetFont(SWGHoloStyle::Font(14)); AssemblyCardName->SetColorAndOpacity(FSlateColor(SWGHoloStyle::BrightText)); }
	SetVisibility(ESlateVisibility::Visible);
	SetIsFocusable(true);
}

void USWGHoloCraftingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Crafting = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGCraftingSubsystem>() : nullptr;
	if (Crafting)
	{
		Crafting->OnSessionStarted.AddUniqueDynamic(this, &USWGHoloCraftingWidget::HandleChanged);
		Crafting->OnDraftPreviewChanged.AddUniqueDynamic(this, &USWGHoloCraftingWidget::HandlePreviewChanged);
		Crafting->OnStageChanged.AddUniqueDynamic(this, &USWGHoloCraftingWidget::HandleChanged);
		Crafting->OnSlotsChanged.AddUniqueDynamic(this, &USWGHoloCraftingWidget::HandleSlotsChanged);
		Crafting->OnSlotResult.AddUniqueDynamic(this, &USWGHoloCraftingWidget::HandleSlotResult);
		Crafting->OnExperimentResult.AddUniqueDynamic(this, &USWGHoloCraftingWidget::HandleExperimentResult);
		Crafting->OnSessionClosed.AddUniqueDynamic(this, &USWGHoloCraftingWidget::HandleSessionClosed);
	}
	CreateButton->OnClicked.AddUniqueDynamic(this, &USWGHoloCraftingWidget::HandleCreateClicked);
	TabButton->OnClicked.AddUniqueDynamic(this, &USWGHoloCraftingWidget::HandleTabClicked);
	WindowButton->OnClicked.AddUniqueDynamic(this, &USWGHoloCraftingWidget::HandleWindowClicked);
	CloseButton->OnClicked.AddUniqueDynamic(this, &USWGHoloCraftingWidget::HandleCloseClicked);
	USWGTreSubsystem* Tre = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGTreSubsystem>() : nullptr;
	for (UButton* Button : { CreateButton.Get(), TabButton.Get(), WindowButton.Get(), CloseButton.Get() })
	{
		if (UObject* Tint = SWGRetailStyle::ApplyHudButton(Button, Tre)) { ButtonTextTints.Add(Tint); }
	}
	Project();
	Refresh();
	SetFocus();
	if (APlayerController* Player = GetOwningPlayer()) { SetUserFocus(Player); }
}

void USWGHoloCraftingWidget::NativeDestruct()
{
	if (Crafting)
	{
		Crafting->OnSessionStarted.RemoveAll(this);
		Crafting->OnDraftPreviewChanged.RemoveAll(this);
		Crafting->OnStageChanged.RemoveAll(this);
		Crafting->OnSlotsChanged.RemoveAll(this);
		Crafting->OnSlotResult.RemoveAll(this);
		Crafting->OnExperimentResult.RemoveAll(this);
		Crafting->OnSessionClosed.RemoveAll(this);
	}
	View.End(*this);
	if (Hologram) { Hologram->Destroy(); Hologram = nullptr; }
	Super::NativeDestruct();
}

void USWGHoloCraftingWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (CurrentStageIndex != INDEX_NONE)
	{
		StageTransitionAlpha = FMath::Min(1.f, StageTransitionAlpha + InDeltaTime / 0.5f);
		PlaceStagePanels(MyGeometry);
	}
}

void USWGHoloCraftingWidget::Project()
{
	APawn* Pawn = GetOwningPlayerPawn();
	if (!Pawn || !GetWorld() || Hologram) { return; }
	const FRotator Facing(0.f, Pawn->GetActorRotation().Yaw, 0.f);
	FVector Location;
	FSWGHoloView::FindProjectorLocation(Pawn, 130.f, 95.f, Location);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Hologram = GetWorld()->SpawnActor<ASWGHoloCraftingActor>(ASWGHoloCraftingActor::StaticClass(), Location, Facing, Params);
	if (!Hologram) { return; }
	Hologram->SetDroidSide(FVector2D(0.f, -1.f));
	View.Begin(*this, Location + Facing.RotateVector(FVector(-300.f, 130.f, 135.f)), Hologram->GetFocusLocation(), 70.f, 0.6f, 0.12f);
}

void USWGHoloCraftingWidget::Refresh()
{
	if (!Crafting || !Hologram) { return; }
	const ESWGCraftingSessionState State = Crafting->GetState();
	if (State == ESWGCraftingSessionState::Assembling)
	{
		OpenAssembly();
		return;
	}
	if (const int32 LaterStage = StageIndex(State); LaterStage > 0)
	{
		OpenLaterStage(State);
		return;
	}
	if (State != ESWGCraftingSessionState::ChoosingSchematic)
	{
		return;
	}
	bAssemblyOpened = false;
	CurrentStageIndex = INDEX_NONE;
	PreviousStageIndex = INDEX_NONE;
	for (UBorder* Panel : StagePanels) { if (Panel) { Panel->SetVisibility(ESlateVisibility::Collapsed); } }
	Hologram->SetAssemblyMode(false);
	Sections.Reset();
	for (const FSWGCraftingSchematicOption& Option : Crafting->GetSchematics())
	{
		FString Path = FPaths::GetPath(Option.TemplatePath);
		Path.RemoveFromStart(TEXT("object/draft_schematic/"));
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("/"), true);
		const FString Section = Parts.Num() > 1 ? Parts[0] + TEXT("/") + Parts[1] : Parts.IsEmpty() ? TEXT("other") : Parts[0];
		Sections.AddUnique(Section);
	}
	Sections.Sort();
	SelectedSectionIndex = FMath::Clamp(SelectedSectionIndex, 0, FMath::Max(0, Sections.Num() - 1));
	SectionList->ClearChildren();
	SectionButtons.Reset();
	SectionForwarders.Reset();
	for (int32 Index = 0; Index < Sections.Num(); ++Index)
	{
		FString Caption = Sections[Index];
		Caption.ReplaceInline(TEXT("/"), TEXT(" / "));
		Caption.ReplaceInline(TEXT("_"), TEXT(" "));
		if (!Caption.IsEmpty()) { Caption[0] = FChar::ToUpper(Caption[0]); }
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Caption));
		Label->SetFont(SWGRetailStyle::Font(17));
		Label->SetColorAndOpacity(FSlateColor(HintColor));
		Button->AddChild(Label);
		USizeBox* Frame = WidgetTree->ConstructWidget<USizeBox>();
		Frame->SetWidthOverride(180.f);
		Frame->SetHeightOverride(36.f);
		Frame->AddChild(Button);
		Button->SetBackgroundColor(Index == SelectedSectionIndex ? FLinearColor(0.08f, 0.45f, 0.6f, 0.9f) : FLinearColor(0.f, 0.12f, 0.18f, 0.7f));
		SectionList->AddChild(Frame);
		USWGTravelPlanetClickForwarder* Forwarder = NewObject<USWGTravelPlanetClickForwarder>(this);
		Forwarder->Action = [this, Index]() { OpenSection(Index); };
		Button->OnClicked.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleClicked);
		SectionButtons.Add(Button);
		SectionForwarders.Add(Forwarder);
	}
	if (Sections.Contains(ActiveSection)) { OpenSection(Sections.IndexOfByKey(ActiveSection)); }
	else { BackToSections(); }
}

void USWGHoloCraftingWidget::OpenSection(int32 Index)
{
	if (!Sections.IsValidIndex(Index) || !Hologram || !Crafting) { return; }
	SelectedSectionIndex = Index;
	ActiveSection = Sections[Index];
	VisibleSchematicIndices.Reset();
	TArray<ASWGHoloCraftingActor::FEntry> Items;
	USWGTreSubsystem* Tre = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGTreSubsystem>() : nullptr;
	const TArray<FSWGCraftingSchematicOption>& Schematics = Crafting->GetSchematics();
	for (int32 SchematicIndex = 0; SchematicIndex < Schematics.Num(); ++SchematicIndex)
	{
		const FSWGCraftingSchematicOption& Option = Schematics[SchematicIndex];
		FString Path = FPaths::GetPath(Option.TemplatePath);
		Path.RemoveFromStart(TEXT("object/draft_schematic/"));
		if (Path != ActiveSection && !Path.StartsWith(ActiveSection + TEXT("/"))) { continue; }
		FString Name = Option.Name;
		Name.RemoveFromStart(TEXT("shared "));
		if (!Name.IsEmpty()) { Name[0] = FChar::ToUpper(Name[0]); }
		FString CraftedPath;
		const uint32 CraftedCrc = Tre && Tre->FindDraftCraftedSharedTemplate(Option.TemplatePath, CraftedPath)
			? FSWGCrc32::HashString(CraftedPath) : 0;
		Items.Add({ Name, CraftedCrc, 0 });
		VisibleSchematicIndices.Add(SchematicIndex);
	}
	SectionList->SetVisibility(ESlateVisibility::Collapsed);
	Hologram->SetItems(Items);
	TabButton->SetIsEnabled(true);
	CreateButton->SetIsEnabled(!Items.IsEmpty());
	if (UTextBlock* Caption = Cast<UTextBlock>(CreateButton->GetContent())) { Caption->SetText(NSLOCTEXT("SWGEmu", "HoloCraftCreate", "Create")); }
	HintText->SetText(NSLOCTEXT("SWGEmu", "HoloCraftItemsHint", "Left/Right to pick an item  •  Enter to create  •  Backspace for sections  •  Tab for window"));
	RefreshSelectionDetails();
	if (!VisibleSchematicIndices.IsEmpty()) { Crafting->RequestDraftPreview({ Schematics[VisibleSchematicIndices[0]].SchematicCrc }); }
}

void USWGHoloCraftingWidget::BackToSections()
{
	ActiveSection.Reset();
	VisibleSchematicIndices.Reset();
	if (Hologram) { Hologram->SetItems({}); }
	SectionList->SetVisibility(ESlateVisibility::Visible);
	CreateButton->SetIsEnabled(!Sections.IsEmpty());
	if (UTextBlock* Caption = Cast<UTextBlock>(CreateButton->GetContent())) { Caption->SetText(NSLOCTEXT("SWGEmu", "HoloCraftOpenSection", "Open Section")); }
	TabButton->SetIsEnabled(false);
	HintText->SetText(NSLOCTEXT("SWGEmu", "HoloCraftSectionsHint", "Up/Down to choose a section  •  Enter to open  •  Tab for window"));
	StatusText->SetText(NSLOCTEXT("SWGEmu", "HoloCraftPickSection", "Choose a crafting section"));
}

void USWGHoloCraftingWidget::OpenAssembly()
{
	if (!Crafting || !Hologram) { return; }
	if (!bAssemblyOpened || CurrentStageIndex != 0)
	{
		if (StagePanels.IsValidIndex(CurrentStageIndex))
		{
			if (const UCanvasPanelSlot* OutgoingSlot = Cast<UCanvasPanelSlot>(StagePanels[CurrentStageIndex]->Slot))
			{
				PreviousStageStartPosition = OutgoingSlot->GetPosition();
			}
		}
		PreviousStageIndex = CurrentStageIndex;
		bAssemblyOpened = true;
		CurrentStageIndex = 0;
		StageTransitionAlpha = 0.f;
		for (int32 Index = 0; Index < StagePanels.Num(); ++Index)
		{
			StagePanels[Index]->SetVisibility(Index == 0 ? ESlateVisibility::Visible
				: Index == PreviousStageIndex ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			StagePanels[Index]->SetIsEnabled(Index == 0);
		}
		Hologram->SetAssemblyMode(true);
		Hologram->RedrawStage();
		Hologram->SetItems({});
		SectionList->SetVisibility(ESlateVisibility::Collapsed);
		if (AssemblyCardPanel) { AssemblyCardPanel->SetVisibility(ESlateVisibility::Visible); }
		ActiveSection.Reset();
		VisibleSchematicIndices.Reset();
		FocusedSlotIndex = 0;
		// Retarget, not Begin: View is already active from Project().
		View.Retarget(AssemblyCameraLocation(GetOwningPlayerPawn()), Hologram->GetAssemblyFocusLocation(), 72.f);
	}
	RefreshAssemblyPane();
}

void USWGHoloCraftingWidget::RefreshAssemblyPane()
{
	if (!Crafting || !Hologram) { return; }
	const TArray<FSWGCraftingSlot>& CraftSlots = Crafting->GetSlots();
	FocusedSlotIndex = FMath::Clamp(FocusedSlotIndex, 0, FMath::Max(0, CraftSlots.Num() - 1));

	int32 TotalRequired = 0;
	int32 TotalFilled = 0;
	TArray<int32> FillCounts;
	for (const FSWGCraftingSlot& SlotEntry : CraftSlots)
	{
		TotalRequired += SlotEntry.RequiredQuantity;
		TotalFilled += FMath::Min(SlotEntry.FilledQuantity, SlotEntry.RequiredQuantity);
		FillCounts.Add(SlotEntry.FilledQuantity);
	}
	Hologram->SetSlotFillCounts(FillCounts);
	Hologram->SetPrototypeObject(Crafting->GetPrototypeId());
	Hologram->SetPrototypeTangibility(TotalRequired > 0 ? float(TotalFilled) / float(TotalRequired) : 0.f);
	RefreshAssemblyCard();
	RefreshAssemblyCandidates();

	HintText->SetText(NSLOCTEXT("SWGEmu", "HoloCraftAssemblyHint",
		"Up/Down to pick a slot  •  Left/Right to pick a resource  •  Enter to place  •  Backspace to remove  •  Tab for window"));
	if (UTextBlock* Caption = Cast<UTextBlock>(CreateButton->GetContent())) { Caption->SetText(NSLOCTEXT("SWGEmu", "HoloCraftAssemble", "Assemble")); }
	CreateButton->SetIsEnabled(Crafting->IsAssemblyReady());
	TabButton->SetIsEnabled(false);
}

void USWGHoloCraftingWidget::RefreshAssemblyCard()
{
	if (!Crafting || !AssemblyCardName || !AssemblySlotList) { return; }
	const TArray<FSWGCraftingSlot>& CraftSlots = Crafting->GetSlots();
	const FSWGCraftingSlot* Focused = CraftSlots.IsValidIndex(FocusedSlotIndex) ? &CraftSlots[FocusedSlotIndex] : nullptr;
	AssemblySlotList->ClearChildren();
	AssemblySlotForwarders.Reset();
	if (!Focused)
	{
		if (AssemblyCardPanel) { AssemblyCardPanel->SetVisibility(ESlateVisibility::Collapsed); }
		StatusText->SetText(NSLOCTEXT("SWGEmu", "HoloCraftNoSlots", "No slots required"));
		return;
	}
	AssemblyCardName->SetText(NSLOCTEXT("SWGEmu", "HoloCraftComponents", "COMPONENTS"));
	const UDataTable* ResourceClasses = LoadObject<UDataTable>(nullptr, *SWGResourceClass::DataTablePath);
	USWGTreSubsystem* Tre = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGTreSubsystem>() : nullptr;
	USWGObjectGraphSubsystem* ObjectGraph = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	TArray<FSWGInventoryEntry> Equipped;
	TArray<FSWGInventoryEntry> Contents;
	SWGInventoryQuery::Gather(GetGameInstance(), Equipped, Contents);
	for (int32 Index = 0; Index < CraftSlots.Num(); ++Index)
	{
		const FSWGCraftingSlot& SlotEntry = CraftSlots[Index];
		int32 MatchCount = 0;
		for (const FSWGInventoryEntry& Entry : Contents)
		{
			MatchCount += ItemFitsSlot(Tre, ResourceClasses, ObjectGraph, Entry.ObjectId, SlotEntry) ? 1 : 0;
		}
		const FLinearColor Color = SlotEntry.IsFull() ? FullColor : SlotEntry.bOptional ? OptionalEmptyColor : RequiredEmptyColor;
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		Button->SetStyle(SWGHoloStyle::ChipStyle());
		Button->SetBackgroundColor(Index == FocusedSlotIndex ? FLinearColor(0.12f, 0.55f, 0.7f) : FLinearColor(0.08f, 0.25f, 0.35f));
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Button->AddChild(Row);
		USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>();
		IconBox->SetWidthOverride(40.f);
		IconBox->SetHeightOverride(40.f);
		if (UTexture2D* Icon = ResourceTypeIcon(Tre, ResourceClasses, SlotEntry.ResourceType))
		{
			UImage* Image = WidgetTree->ConstructWidget<UImage>();
			Image->SetBrushFromTexture(Icon);
			IconBox->AddChild(Image);
		}
		else
		{
			UTextBlock* Glyph = WidgetTree->ConstructWidget<UTextBlock>();
			Glyph->SetText(FText::FromString(SlotEntry.Kind == ESWGDraftSlotKind::Resource ? TEXT("◆") : TEXT("◇")));
			Glyph->SetFont(SWGHoloStyle::Font(23));
			Glyph->SetColorAndOpacity(FSlateColor(Color));
			IconBox->AddChild(Glyph);
		}
		Row->AddChildToHorizontalBox(IconBox)->SetPadding(FMargin(4.f, 2.f, 8.f, 2.f));
		UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>();
		UHorizontalBoxSlot* TextsSlot = Row->AddChildToHorizontalBox(Texts);
		TextsSlot->SetVerticalAlignment(VAlign_Center);
		// Fill, so the row's width bounds the text and long slot names wrap.
		TextsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UTextBlock* Name = WidgetTree->ConstructWidget<UTextBlock>();
		Name->SetText(FText::FromString(SlotEntry.Name + (SlotEntry.bOptional ? TEXT(" (optional)") : TEXT(""))));
		Name->SetFont(SWGHoloStyle::Font(13));
		Name->SetColorAndOpacity(FSlateColor(SWGHoloStyle::BrightText));
		Name->SetAutoWrapText(true);
		Texts->AddChild(Name);
		const FString Category = SlotCategoryName(Tre, ResourceClasses, SlotEntry);
		const FText Count = FText::Format(NSLOCTEXT("SWGEmu", "HoloCraftCardQty", "{0} / {1}"), FText::AsNumber(SlotEntry.FilledQuantity), FText::AsNumber(SlotEntry.RequiredQuantity));
		UTextBlock* Quantity = WidgetTree->ConstructWidget<UTextBlock>();
		Quantity->SetText(SlotEntry.FilledQuantity > 0 || Category.IsEmpty() ? Count
			: FText::Format(NSLOCTEXT("SWGEmu", "HoloCraftCardQtyCategory", "{0}  •  {1}"), Count, FText::FromString(Category)));
		Quantity->SetFont(SWGHoloStyle::Font(11, false));
		Quantity->SetColorAndOpacity(FSlateColor(Color));
		Texts->AddChild(Quantity);
		Button->SetToolTip(BuildSlotToolTip(WidgetTree, SlotEntry, Category, ResourceClasses, MatchCount));
		AssemblySlotList->AddChild(Button);
		USWGTravelPlanetClickForwarder* Forwarder = NewObject<USWGTravelPlanetClickForwarder>(this);
		Forwarder->Action = [this, Index]() { FocusedSlotIndex = Index; Hologram->SetSelectedSlotForCandidates(Index); RefreshAssemblyCard(); };
		Button->OnClicked.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleClicked);
		AssemblySlotForwarders.Add(Forwarder);
	}
	if (AssemblyCardPanel) { AssemblyCardPanel->SetVisibility(ESlateVisibility::Visible); }
	StatusText->SetText(FText::Format(NSLOCTEXT("SWGEmu", "HoloCraftSlotStatus", "{0}  •  {1} / {2}"),
		FText::FromString(Focused->Name), FText::AsNumber(Focused->FilledQuantity), FText::AsNumber(Focused->RequiredQuantity)));
}

void USWGHoloCraftingWidget::PlaceStagePanels(const FGeometry& MyGeometry)
{
	if (!StagePanels.IsValidIndex(CurrentStageIndex) || !Hologram) { return; }
	UBorder* Current = StagePanels[CurrentStageIndex];
	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController) { return; }
	FVector2D Screen;
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PlayerController, Hologram->GetPrototypeLocation(), Screen, /*bPlayerViewportRelative=*/true))
	{
		Hologram->SetComponentPanelRayTargets({});
		Hologram->SetContributionOrigins({});
		return;
	}
	// Right of the prototype: the candidate rows fan out to its left. The gap
	// clears the model's projected half-width (~35 cm spinning) plus a margin.
	float Gap = 90.f;
	{
		FVector CameraLocation;
		FRotator CameraRotation;
		PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
		FVector2D EdgeScreen;
		if (UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PlayerController,
			Hologram->GetPrototypeLocation() + FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Y) * 35.f, EdgeScreen, /*bPlayerViewportRelative=*/true))
		{
			Gap = FMath::Abs(EdgeScreen.X - Screen.X) + 30.f;
		}
	}
	const FVector2D CardSize = Current->GetDesiredSize();
	const FVector2D ViewSize = MyGeometry.GetLocalSize();
	const FVector2D RestPosition(
		FMath::Clamp(Screen.X + Gap, 12.f, FMath::Max(12.f, ViewSize.X - CardSize.X - 12.f)),
		FMath::Clamp(Screen.Y - CardSize.Y * 0.5f, 12.f, FMath::Max(12.f, ViewSize.Y - CardSize.Y - 12.f)));
	const FVector2D Position = RestPosition + FVector2D((1.f - StageTransitionAlpha) * 70.f, 0.f);
	if (UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(Current->Slot))
	{
		CardSlot->SetAlignment(FVector2D::ZeroVector);
		CardSlot->SetPosition(Position);
		CardSlot->SetZOrder(21);
	}
	Current->SetRenderOpacity(StageTransitionAlpha);
	Current->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	Current->SetRenderTransformAngle(0.f);
	Current->SetRenderScale(FVector2D(1.f, 1.f));
	Current->SetRenderShear(FVector2D::ZeroVector);
	if (StagePanels.IsValidIndex(PreviousStageIndex))
	{
		UBorder* Previous = StagePanels[PreviousStageIndex];
		const FVector2D StackedPosition(FMath::Max(-24.f, RestPosition.X - 75.f), RestPosition.Y + 55.f);
		if (UCanvasPanelSlot* CardSlot = Cast<UCanvasPanelSlot>(Previous->Slot))
		{
			CardSlot->SetAlignment(FVector2D::ZeroVector);
			CardSlot->SetPosition(FMath::Lerp(PreviousStageStartPosition, StackedPosition, StageTransitionAlpha));
			CardSlot->SetZOrder(20);
		}
		Previous->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Previous->SetRenderTransformAngle(0.f);
		Previous->SetRenderScale(FVector2D(FMath::Lerp(1.f, 0.92f, StageTransitionAlpha)));
		Previous->SetRenderShear(FVector2D::ZeroVector);
		Previous->SetRenderOpacity(FMath::Lerp(1.f, 0.55f, StageTransitionAlpha));
	}
	if (CardSize.X <= 0.f || CardSize.Y <= 0.f) { return; }
	FVector CameraLocation;
	FRotator CameraRotation;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const float Depth = FVector::Distance(CameraLocation, Hologram->GetPrototypeLocation());
	const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
	TArray<FVector> RayTargets;
	for (const FVector2D& Corner : { Position, Position + FVector2D(CardSize.X, 0.f),
		Position + FVector2D(0.f, CardSize.Y), Position + CardSize })
	{
		FVector Origin, Direction;
		if (PlayerController->DeprojectScreenPositionToWorld(Corner.X * ViewportScale, Corner.Y * ViewportScale, Origin, Direction))
		{
			RayTargets.Add(Origin + Direction * Depth);
		}
	}
	Hologram->SetComponentPanelRayTargets(RayTargets);
	TArray<FVector> Origins;
	if (CurrentStageIndex == 0 && Crafting && AssemblySlotList)
	{
		const TArray<FSWGCraftingSlot>& Slots = Crafting->GetSlots();
		Origins.SetNum(Slots.Num());
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (Slots[Index].FilledQuantity <= 0 || !AssemblySlotList->GetChildAt(Index)) { continue; }
			const FGeometry Row = AssemblySlotList->GetChildAt(Index)->GetCachedGeometry();
			if (Row.GetLocalSize().IsNearlyZero()) { continue; }
			FVector2D Pixel, Viewport;
			USlateBlueprintLibrary::AbsoluteToViewport(this, Row.LocalToAbsolute(FVector2D(0.f, Row.GetLocalSize().Y * 0.5f)), Pixel, Viewport);
			FVector Origin, Direction;
			if (PlayerController->DeprojectScreenPositionToWorld(Pixel.X, Pixel.Y, Origin, Direction)) { Origins[Index] = Origin + Direction * Depth; }
		}
	}
	Hologram->SetContributionOrigins(Origins);
}

UButton* USWGHoloCraftingWidget::AddStageAction(UPanelWidget* Parent, const FText& Caption, TFunction<void()> Action)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>();
	Button->SetStyle(SWGHoloStyle::ChipStyle());
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
	Label->SetText(Caption);
	Label->SetFont(SWGHoloStyle::Font(12));
	Label->SetColorAndOpacity(FSlateColor(SWGHoloStyle::BrightText));
	Label->SetAutoWrapText(true);
	Button->AddChild(Label);
	Parent->AddChild(Button);
	USWGTravelPlanetClickForwarder* Forwarder = NewObject<USWGTravelPlanetClickForwarder>(this);
	Forwarder->Action = MoveTemp(Action);
	Button->OnClicked.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleClicked);
	StageForwarders.Add(Forwarder);
	return Button;
}

void USWGHoloCraftingWidget::OpenLaterStage(ESWGCraftingSessionState State)
{
	const int32 NextStage = StageIndex(State);
	if (!StagePanels.IsValidIndex(NextStage)) { return; }
	if (!bAssemblyOpened)
	{
		bAssemblyOpened = true;
		Hologram->SetAssemblyMode(true);
		Hologram->SetItems({});
		View.Retarget(AssemblyCameraLocation(GetOwningPlayerPawn()), Hologram->GetAssemblyFocusLocation(), 72.f);
	}
	if (NextStage != CurrentStageIndex)
	{
		if (StagePanels.IsValidIndex(CurrentStageIndex))
		{
			if (const UCanvasPanelSlot* OutgoingSlot = Cast<UCanvasPanelSlot>(StagePanels[CurrentStageIndex]->Slot))
			{
				PreviousStageStartPosition = OutgoingSlot->GetPosition();
			}
		}
		PreviousStageIndex = CurrentStageIndex;
		CurrentStageIndex = NextStage;
		StageTransitionAlpha = 0.f;
		FocusedStageRow = 0;
		if (NextStage == 1) { ExperimentAllocations.Init(0, Crafting->GetExperimentGroups().Num()); bExperimentPending = false; }
		if (NextStage == 2) { CustomizeVarValues = Crafting->GetCustomizationVarDefaults(); }
		for (int32 Index = 0; Index < StagePanels.Num(); ++Index)
		{
			StagePanels[Index]->SetVisibility(Index == CurrentStageIndex ? ESlateVisibility::Visible
				: Index == PreviousStageIndex ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			StagePanels[Index]->SetIsEnabled(Index == CurrentStageIndex);
		}
		Hologram->SetCandidateRows({});
		CandidateIdsBySlot.Reset();
		Hologram->RedrawStage();
	}
	else if (CurrentStageIndex == 2 && CustomizeNameBox)
	{
		CustomizeName = CustomizeNameBox->GetText().ToString();
	}
	SectionList->SetVisibility(ESlateVisibility::Collapsed);
	Hologram->SetPrototypeObject(Crafting->GetPrototypeId());
	Hologram->SetPrototypeTangibility(1.f);
	RebuildLaterStagePanel();
	CreateButton->SetIsEnabled(true);
	TabButton->SetIsEnabled(NextStage < 3);
	if (UTextBlock* Caption = Cast<UTextBlock>(TabButton->GetContent())) { Caption->SetText(NSLOCTEXT("SWGEmu", "HoloCraftContinue", "Continue")); }
	if (UTextBlock* Caption = Cast<UTextBlock>(CreateButton->GetContent()))
	{
		Caption->SetText(NextStage == 1 ? NSLOCTEXT("SWGEmu", "HoloCraftExperiment", "Experiment")
			: NextStage == 2 ? NSLOCTEXT("SWGEmu", "HoloCraftApply", "Apply")
			: NSLOCTEXT("SWGEmu", "HoloCraftCreatePrototype", "Create Prototype"));
	}
	HintText->SetText(NextStage == 1 ? NSLOCTEXT("SWGEmu", "HoloCraftExperimentHint", "Up/Down: attribute  •  Left/Right: points  •  Enter: experiment  •  Continue: next stage")
		: NextStage == 2 ? NSLOCTEXT("SWGEmu", "HoloCraftCustomizeHint", "Choose a template, name and colours  •  Apply  •  Continue")
		: NSLOCTEXT("SWGEmu", "HoloCraftFinishHint", "Create the item, practice for XP, or save a factory schematic"));
	StatusText->SetText(NextStage == 1 ? NSLOCTEXT("SWGEmu", "HoloCraftExperimentStatus", "Experimentation")
		: NextStage == 2 ? NSLOCTEXT("SWGEmu", "HoloCraftCustomizeStatus", "Customization")
		: NSLOCTEXT("SWGEmu", "HoloCraftFinishStatus", "Ready to finish"));
}

void USWGHoloCraftingWidget::RebuildLaterStagePanel()
{
	if (!Crafting || !StageBodies.IsValidIndex(CurrentStageIndex) || CurrentStageIndex < 1) { return; }
	UVerticalBox* Body = StageBodies[CurrentStageIndex];
	Body->ClearChildren();
	StageForwarders.Reset();
	auto AddText = [this, Body](const FText& Value, int32 Size = 12)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(Value);
		Label->SetFont(SWGHoloStyle::Font(Size));
		Label->SetColorAndOpacity(FSlateColor(SWGHoloStyle::BrightText));
		Label->SetAutoWrapText(true);
		Body->AddChild(Label);
	};
	if (CurrentStageIndex == 1)
	{
		AddText(NSLOCTEXT("SWGEmu", "HoloExperimentTitle", "EXPERIMENTATION"), 16);
		const TArray<FSWGCraftingExperimentGroup>& Groups = Crafting->GetExperimentGroups();
		ExperimentAllocations.SetNum(Groups.Num());
		int32 Allocated = 0;
		for (int32 Points : ExperimentAllocations) { Allocated += Points; }
		const int32 Available = FMath::Max(0, Crafting->GetExperimentPointsRemaining() - Allocated);
		AddText(FText::Format(NSLOCTEXT("SWGEmu", "HoloExperimentPoints", "Points: {0} / {1}    Failure: {2}%"),
			FText::AsNumber(Available), FText::AsNumber(Crafting->GetExperimentPointsTotal()),
			FText::AsNumber(FMath::RoundToInt(Crafting->GetFailureRate()))));
		for (int32 Index = 0; Index < Groups.Num(); ++Index)
		{
			const FSWGCraftingExperimentGroup& Group = Groups[Index];
			FString Name = Group.Title;
			FText Title = FText::FromString(GetGameInstance() && GetGameInstance()->GetSubsystem<USWGTreSubsystem>()
				? GetGameInstance()->GetSubsystem<USWGTreSubsystem>()->LookupString(TEXT("crafting"), Name) : FString());
			if (Title.IsEmpty()) { Name.RemoveFromStart(TEXT("exp_")); Name.ReplaceInline(TEXT("_"), TEXT(" ")); Title = FText::FromString(Name); }
			AddText(FText::Format(NSLOCTEXT("SWGEmu", "HoloExperimentRow", "{0}{1}: {2}% / {3}%   +{4}"),
				FText::FromString(Index == FocusedStageRow ? TEXT("◆ ") : TEXT("  ")), Title,
				FText::AsNumber(FMath::RoundToInt(Group.CurrentPercent * 100.f)),
				FText::AsNumber(FMath::RoundToInt(Group.MaxPercent * 100.f)), FText::AsNumber(ExperimentAllocations[Index])));
			UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>();
			Bar->SetPercent(FMath::Clamp(Group.CurrentPercent, 0.f, 1.f));
			Body->AddChild(Bar);
			UHorizontalBox* Controls = WidgetTree->ConstructWidget<UHorizontalBox>();
			Body->AddChild(Controls);
			for (int32 Delta : {-1, 1})
			{
				UButton* Button = AddStageAction(Controls, FText::FromString(Delta < 0 ? TEXT(" − ") : TEXT(" + ")),
					[this, Index, Delta]() { FocusedStageRow = Index; AdjustStageValue(Delta); });
				Button->SetIsEnabled(!bExperimentPending && (Delta < 0 ? ExperimentAllocations[Index] > 0 : Available > 0));
			}
		}
		UButton* ExperimentButton = AddStageAction(Body, NSLOCTEXT("SWGEmu", "HoloExperimentApply", "Experiment"), [this]() { ApplyExperiment(); });
		ExperimentButton->SetIsEnabled(Allocated > 0 && !bExperimentPending);
		AddStageAction(Body, NSLOCTEXT("SWGEmu", "HoloExperimentContinue", "Continue"), [this]() { Crafting->Assemble(); });
	}
	else if (CurrentStageIndex == 2)
	{
		AddText(NSLOCTEXT("SWGEmu", "HoloCustomizeTitle", "CUSTOMIZATION"), 16);
		CustomizeNameBox = WidgetTree->ConstructWidget<UEditableTextBox>();
		CustomizeNameBox->SetHintText(NSLOCTEXT("SWGEmu", "HoloCustomizeName", "Item name"));
		CustomizeNameBox->SetText(FText::FromString(CustomizeName));
		Body->AddChild(CustomizeNameBox);
		const TArray<FString>& Templates = Crafting->GetTemplateChoices();
		for (int32 Index = 0; Index < Templates.Num(); ++Index)
		{
			FString Name = FPaths::GetBaseFilename(Templates[Index]);
			AddStageAction(Body, FText::FromString(FString(Index == SelectedTemplateChoice ? TEXT("◆ ") : TEXT("◇ ")) + Name),
				[this, Index]() { CustomizeName = CustomizeNameBox->GetText().ToString(); SelectedTemplateChoice = Index; RebuildLaterStagePanel(); });
		}
		const TArray<FString>& Names = Crafting->GetCustomizationVarNames();
		const TArray<int32>& Defaults = Crafting->GetCustomizationVarDefaults();
		const TArray<int32>& Palettes = Crafting->GetCustomizationPaletteCounts();
		if (CustomizeVarValues.Num() != Names.Num()) { CustomizeVarValues = Defaults; CustomizeVarValues.SetNum(Names.Num()); }
		for (int32 Index = 0; Index < Names.Num(); ++Index)
		{
			AddText(FText::Format(NSLOCTEXT("SWGEmu", "HoloCustomizeVariable", "{0}: {1}"),
				FText::FromString(Names[Index]), FText::AsNumber(CustomizeVarValues[Index])));
			UHorizontalBox* Controls = WidgetTree->ConstructWidget<UHorizontalBox>();
			Body->AddChild(Controls);
			for (int32 Delta : {-1, 1})
			{
				const int32 PaletteCount = Palettes.IsValidIndex(Index) ? Palettes[Index] : 0;
				AddStageAction(Controls, FText::FromString(Delta < 0 ? TEXT(" − ") : TEXT(" + ")),
					[this, Index, Delta, PaletteCount]()
					{
						CustomizeName = CustomizeNameBox->GetText().ToString();
						CustomizeVarValues[Index] = FMath::Clamp(CustomizeVarValues[Index] + Delta, 0, PaletteCount > 0 ? PaletteCount - 1 : 100);
						RebuildLaterStagePanel();
					});
			}
		}
		AddStageAction(Body, NSLOCTEXT("SWGEmu", "HoloCustomizeApply", "Apply customization"), [this]() { ApplyCustomization(); });
		AddStageAction(Body, NSLOCTEXT("SWGEmu", "HoloCustomizeContinue", "Continue"), [this]() { Crafting->Assemble(); });
	}
	else
	{
		AddText(NSLOCTEXT("SWGEmu", "HoloFinishTitle", "FINAL CREATION"), 16);
		AddText(FText::FromString(Crafting->GetPrototypeName()));
		AddStageAction(Body, NSLOCTEXT("SWGEmu", "HoloFinishCreate", "Create prototype"), [this]() { Crafting->CreatePrototype(false); });
		AddStageAction(Body, NSLOCTEXT("SWGEmu", "HoloFinishPractice", "Practice for XP"), [this]() { Crafting->CreatePrototype(true); });
		UButton* Factory = AddStageAction(Body, NSLOCTEXT("SWGEmu", "HoloFinishFactory", "Create factory schematic"),
			[this]() { Crafting->CreateManufactureSchematic(); });
		Factory->SetIsEnabled(Crafting->GetAllowFactoryRun());
	}
}

void USWGHoloCraftingWidget::AdjustStageValue(int32 Direction)
{
	if (!Crafting || CurrentStageIndex != 1 || !ExperimentAllocations.IsValidIndex(FocusedStageRow) || bExperimentPending) { return; }
	int32 Allocated = 0;
	for (int32 Points : ExperimentAllocations) { Allocated += Points; }
	const int32 Available = Crafting->GetExperimentPointsRemaining() - Allocated;
	if (Direction > 0 && Available <= 0) { return; }
	ExperimentAllocations[FocusedStageRow] = FMath::Max(0, ExperimentAllocations[FocusedStageRow] + Direction);
	RebuildLaterStagePanel();
}

void USWGHoloCraftingWidget::ApplyExperiment()
{
	if (!Crafting || bExperimentPending) { return; }
	TArray<FSWGCraftingExperimentRow> Rows;
	for (int32 Index = 0; Index < ExperimentAllocations.Num(); ++Index)
	{
		if (ExperimentAllocations[Index] > 0)
		{
			FSWGCraftingExperimentRow& Row = Rows.AddDefaulted_GetRef();
			Row.RowIndex = Index;
			Row.Points = ExperimentAllocations[Index];
		}
	}
	if (Crafting->Experiment(Rows)) { bExperimentPending = true; RebuildLaterStagePanel(); }
}

void USWGHoloCraftingWidget::ApplyCustomization()
{
	if (!Crafting) { return; }
	CustomizeName = CustomizeNameBox ? CustomizeNameBox->GetText().ToString() : CustomizeName;
	TArray<FSWGCraftingCustomizationEdit> Edits;
	for (int32 Index = 0; Index < CustomizeVarValues.Num(); ++Index)
	{
		FSWGCraftingCustomizationEdit& Edit = Edits.AddDefaulted_GetRef();
		Edit.Index = Index;
		Edit.Value = CustomizeVarValues[Index];
	}
	const uint8 TemplateChoice = SelectedTemplateChoice == INDEX_NONE ? 0xFF : static_cast<uint8>(SelectedTemplateChoice);
	Crafting->Customize(CustomizeName, TemplateChoice, 1, Edits);
}

void USWGHoloCraftingWidget::RefreshAssemblyCandidates()
{
	if (!Crafting || !Hologram) { return; }
	const TArray<FSWGCraftingSlot>& CraftSlots = Crafting->GetSlots();
	const UDataTable* ResourceClasses = LoadObject<UDataTable>(nullptr, *SWGResourceClass::DataTablePath);
	USWGObjectGraphSubsystem* ObjectGraph = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	TArray<FSWGInventoryEntry> Equipped;
	TArray<FSWGInventoryEntry> Contents;
	SWGInventoryQuery::Gather(GetGameInstance(), Equipped, Contents);
	USWGTreSubsystem* Tre = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGTreSubsystem>() : nullptr;
	CandidateIdsBySlot.Reset();
	TArray<TArray<ASWGHoloCraftingActor::FEntry>> RowsBySlot;
	for (const FSWGCraftingSlot& SlotEntry : CraftSlots)
	{
		TArray<int64>& SlotCandidateIds = CandidateIdsBySlot.AddDefaulted_GetRef();
		TArray<ASWGHoloCraftingActor::FEntry>& Row = RowsBySlot.AddDefaulted_GetRef();
		for (const FSWGInventoryEntry& Entry : Contents)
		{
			if (Row.Num() >= MaxCandidatesPerRow) { break; }
			if (!ItemFitsSlot(Tre, ResourceClasses, ObjectGraph, Entry.ObjectId, SlotEntry)) { continue; }
			SlotCandidateIds.Add(Entry.ObjectId);
			Row.Add({ Entry.Label(), 0, Entry.ObjectId });
		}
	}
	// One row per slot, laid on the holo floor — the selected slot's row is
	// pulled to the front (SetSelectedSlotForCandidates), the rest recede
	// behind it.
	Hologram->SetCandidateRows(RowsBySlot);
	Hologram->SetSelectedSlotForCandidates(FocusedSlotIndex);
}

void USWGHoloCraftingWidget::SelectSlotNext(int32 Direction)
{
	if (!Crafting || !Hologram) { return; }
	const int32 Count = Crafting->GetSlots().Num();
	if (Count == 0) { return; }
	FocusedSlotIndex = (FocusedSlotIndex + Direction + Count) % Count;
	// A cheap reposition (the candidate rows already exist for every slot,
	// see RefreshAssemblyCandidates) — no model reload, just which row is
	// frontmost.
	Hologram->SetSelectedSlotForCandidates(FocusedSlotIndex);
	RefreshAssemblyCard();
}

void USWGHoloCraftingWidget::SelectCandidateNext(int32 Direction)
{
	if (!Hologram || !CandidateIdsBySlot.IsValidIndex(FocusedSlotIndex)) { return; }
	const int32 Count = CandidateIdsBySlot[FocusedSlotIndex].Num();
	if (Count == 0) { return; }
	Hologram->SetSelectedCandidateColumn(FMath::Clamp(Hologram->GetSelectedCandidateColumn() + Direction, 0, Count - 1));
}

void USWGHoloCraftingWidget::AddSelectedCandidateToFocusedSlot()
{
	if (!Crafting || !Hologram || !CandidateIdsBySlot.IsValidIndex(FocusedSlotIndex)) { return; }
	const TArray<int64>& SlotCandidateIds = CandidateIdsBySlot[FocusedSlotIndex];
	if (!SlotCandidateIds.IsValidIndex(Hologram->GetSelectedCandidateColumn())) { return; }
	Crafting->AddIngredient(SlotCandidateIds[Hologram->GetSelectedCandidateColumn()], FocusedSlotIndex);
}

void USWGHoloCraftingWidget::RemoveFocusedSlotItem()
{
	if (!Crafting) { return; }
	const TArray<FSWGCraftingSlot>& CraftSlots = Crafting->GetSlots();
	if (!CraftSlots.IsValidIndex(FocusedSlotIndex) || CraftSlots[FocusedSlotIndex].FilledObjectIds.IsEmpty()) { return; }
	Crafting->RemoveIngredient(FocusedSlotIndex, CraftSlots[FocusedSlotIndex].FilledObjectIds[0]);
}

void USWGHoloCraftingWidget::RefreshSelectionDetails()
{
	if (!Crafting || !Hologram || ActiveSection.IsEmpty()) { return; }
	const int32 ItemIndex = Hologram->GetSelectedIndex();
	if (!VisibleSchematicIndices.IsValidIndex(ItemIndex)) { return; }
	const FSWGCraftingSchematicOption& Option = Crafting->GetSchematics()[VisibleSchematicIndices[ItemIndex]];
	if (const FSWGCraftingDraftSlotsIn* Preview = Crafting->GetDraftSlotsPreview(Option.SchematicCrc))
	{
		FString Name = Option.Name;
		Name.RemoveFromStart(TEXT("shared "));
		if (!Name.IsEmpty()) { Name[0] = FChar::ToUpper(Name[0]); }
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("%s  •  Complexity %d  •  %d ingredients"),
			*Name, Preview->Complexity, Preview->Slots.Num())));
	}
	else { StatusText->SetText(FText::FromString(Option.Name)); }
}

void USWGHoloCraftingWidget::SelectNext(int32 Direction)
{
	if (ActiveSection.IsEmpty())
	{
		SelectedSectionIndex = FMath::Clamp(SelectedSectionIndex + Direction, 0, FMath::Max(0, Sections.Num() - 1));
		for (int32 Index = 0; Index < SectionButtons.Num(); ++Index)
		{
			SectionButtons[Index]->SetBackgroundColor(Index == SelectedSectionIndex ? FLinearColor(0.08f, 0.45f, 0.6f, 0.9f) : FLinearColor(0.f, 0.12f, 0.18f, 0.7f));
		}
		return;
	}
	if (!Hologram || VisibleSchematicIndices.IsEmpty()) { return; }
	Hologram->SetSelectedIndex(FMath::Clamp(Hologram->GetSelectedIndex() + Direction, 0, VisibleSchematicIndices.Num() - 1));
	RefreshSelectionDetails();
	Crafting->RequestDraftPreview({ Crafting->GetSchematics()[VisibleSchematicIndices[Hologram->GetSelectedIndex()]].SchematicCrc });
}

void USWGHoloCraftingWidget::Create()
{
	if (CurrentStageIndex == 1) { ApplyExperiment(); return; }
	if (CurrentStageIndex == 2) { ApplyCustomization(); return; }
	if (CurrentStageIndex == 3) { if (Crafting) { Crafting->CreatePrototype(false); } return; }
	if (Crafting && Crafting->GetState() == ESWGCraftingSessionState::Assembling)
	{
		// The Create button doubles as "Assemble" once every required slot is
		// full (button-only — Enter is bound to placing a resource instead,
		// see NativeOnKeyDown, so the frequent action and the deliberate one
		// don't collide on the same key).
		if (Crafting->IsAssemblyReady()) { Crafting->Assemble(); }
		return;
	}
	if (ActiveSection.IsEmpty()) { OpenSection(SelectedSectionIndex); return; }
	if (Crafting && Hologram && VisibleSchematicIndices.IsValidIndex(Hologram->GetSelectedIndex()))
	{
		Crafting->SelectSchematic(VisibleSchematicIndices[Hologram->GetSelectedIndex()]);
	}
}

void USWGHoloCraftingWidget::Dismiss()
{
	RemoveFromParent();
	OnClosed.Broadcast();
}

void USWGHoloCraftingWidget::Close()
{
	if (Crafting && Crafting->IsSessionOpen()) { Crafting->Cancel(); }
	Dismiss();
}

void USWGHoloCraftingWidget::SetSuspended(bool bInSuspended)
{
	if (bSuspended == bInSuspended) { return; }
	bSuspended = bInSuspended;
	if (bSuspended)
	{
		View.End(*this);
		if (Hologram) { Hologram->SetActorHiddenInGame(true); }
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (Hologram && GetOwningPlayerPawn())
	{
		Hologram->SetActorHiddenInGame(false);
		const FRotator Facing(0.f, GetOwningPlayerPawn()->GetActorRotation().Yaw, 0.f);
		const bool bLaterStage = CurrentStageIndex != INDEX_NONE;
		View.Begin(*this, bLaterStage ? AssemblyCameraLocation(GetOwningPlayerPawn())
			: Hologram->GetActorLocation() + Facing.RotateVector(FVector(-300.f, 130.f, 135.f)),
			bLaterStage ? Hologram->GetAssemblyFocusLocation() : Hologram->GetFocusLocation(),
			bLaterStage ? 72.f : 70.f, 0.6f, 0.12f);
	}
	SetVisibility(ESlateVisibility::Visible);
	SetFocus();
}

FReply USWGHoloCraftingWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	const bool bAssembling = Crafting && Crafting->GetState() == ESWGCraftingSessionState::Assembling;
	const FKey Key = Event.GetKey();
	if (CurrentStageIndex > 0)
	{
		if (CurrentStageIndex == 1 && (Key == EKeys::Up || Key == EKeys::Down || Key == EKeys::W || Key == EKeys::S))
		{
			FocusedStageRow = FMath::Clamp(FocusedStageRow + (Key == EKeys::Up || Key == EKeys::W ? -1 : 1),
				0, FMath::Max(0, Crafting->GetExperimentGroups().Num() - 1));
			RebuildLaterStagePanel();
			return FReply::Handled();
		}
		if (CurrentStageIndex == 1 && (Key == EKeys::Left || Key == EKeys::Right))
		{
			AdjustStageValue(Key == EKeys::Left ? -1 : 1);
			return FReply::Handled();
		}
		if (Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Bottom) { Create(); return FReply::Handled(); }
		if ((Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right) && CurrentStageIndex < 3)
		{
			Crafting->Assemble();
			return FReply::Handled();
		}
		if (Key == EKeys::Escape) { Close(); return FReply::Handled(); }
		if (Key == EKeys::Tab || Key == EKeys::Gamepad_FaceButton_Top) { OnSwitchToWindow.Broadcast(); return FReply::Handled(); }
		return Super::NativeOnKeyDown(Geometry, Event);
	}
	if (Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Up) { bAssembling ? SelectSlotNext(-1) : SelectNext(-1); return FReply::Handled(); }
	if (Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Down) { bAssembling ? SelectSlotNext(1) : SelectNext(1); return FReply::Handled(); }
	if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left) { bAssembling ? SelectCandidateNext(-1) : SelectNext(-1); return FReply::Handled(); }
	if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right) { bAssembling ? SelectCandidateNext(1) : SelectNext(1); return FReply::Handled(); }
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		bAssembling ? AddSelectedCandidateToFocusedSlot() : Create();
		return FReply::Handled();
	}
	if (Key == EKeys::Tab || Key == EKeys::Gamepad_FaceButton_Top) { OnSwitchToWindow.Broadcast(); return FReply::Handled(); }
	if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right)
	{
		if (bAssembling) { RemoveFocusedSlotItem(); }
		else if (ActiveSection.IsEmpty()) { Close(); }
		else { BackToSections(); }
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geometry, Event);
}

FReply USWGHoloCraftingWidget::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const int32 Direction = Event.GetWheelDelta() > 0.f ? -1 : 1;
	if (CurrentStageIndex > 0) { return Super::NativeOnMouseWheel(Geometry, Event); }
	if (Crafting && Crafting->GetState() == ESWGCraftingSessionState::Assembling) { SelectCandidateNext(Direction); }
	else { SelectNext(Direction); }
	return FReply::Handled();
}

FReply USWGHoloCraftingWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	APlayerController* PlayerController = GetOwningPlayer();
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && PlayerController && Hologram
		&& (!Crafting || Crafting->GetState() != ESWGCraftingSessionState::Assembling) && !ActiveSection.IsEmpty())
	{
		FVector2D Pixel, Viewport;
		USlateBlueprintLibrary::AbsoluteToViewport(this, Event.GetScreenSpacePosition(), Pixel, Viewport);
		int32 Best = INDEX_NONE;
		float BestDistance = 55.f;
		for (int32 Index = 0; Index < VisibleSchematicIndices.Num(); ++Index)
		{
			FVector World;
			FVector2D Screen;
			if (Hologram->GetItemLocation(Index, World) && PlayerController->ProjectWorldLocationToScreen(World, Screen))
			{
				const float Distance = FVector2D::Distance(Pixel, Screen);
				if (Distance < BestDistance) { Best = Index; BestDistance = Distance; }
			}
		}
		if (Best != INDEX_NONE) { Hologram->SetSelectedIndex(Best); RefreshSelectionDetails(); }
	}
	else if (Event.GetEffectingButton() == EKeys::LeftMouseButton && PlayerController && Hologram
		&& Crafting && Crafting->GetState() == ESWGCraftingSessionState::Assembling
		&& CandidateIdsBySlot.IsValidIndex(FocusedSlotIndex))
	{
		// The front row (the focused slot's candidates) — clicking one
		// places it directly, one click to grab and fill.
		FVector2D Pixel, Viewport;
		USlateBlueprintLibrary::AbsoluteToViewport(this, Event.GetScreenSpacePosition(), Pixel, Viewport);
		const int32 Count = CandidateIdsBySlot[FocusedSlotIndex].Num();
		int32 BestColumn = INDEX_NONE;
		float BestDistance = 55.f;
		for (int32 Column = 0; Column < Count; ++Column)
		{
			FVector World;
			FVector2D Screen;
			if (Hologram->GetCandidateLocation(FocusedSlotIndex, Column, World) && PlayerController->ProjectWorldLocationToScreen(World, Screen))
			{
				const float Distance = FVector2D::Distance(Pixel, Screen);
				if (Distance < BestDistance) { BestColumn = Column; BestDistance = Distance; }
			}
		}
		if (BestColumn != INDEX_NONE)
		{
			Hologram->SetSelectedCandidateColumn(BestColumn);
			AddSelectedCandidateToFocusedSlot();
		}
	}
	return FReply::Handled().SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

FReply USWGHoloCraftingWidget::NativeOnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && !ActiveSection.IsEmpty()
		&& (!Crafting || Crafting->GetState() != ESWGCraftingSessionState::Assembling))
	{
		Create();
	}
	return FReply::Handled();
}

void USWGHoloCraftingWidget::HandleChanged() { Refresh(); }
void USWGHoloCraftingWidget::HandlePreviewChanged() { RefreshSelectionDetails(); }
void USWGHoloCraftingWidget::HandleSlotsChanged()
{
	if (Crafting && Crafting->GetState() == ESWGCraftingSessionState::Assembling) { RefreshAssemblyPane(); }
	else if (CurrentStageIndex > 0)
	{
		if (CurrentStageIndex == 2 && CustomizeNameBox) { CustomizeName = CustomizeNameBox->GetText().ToString(); }
		RebuildLaterStagePanel();
	}
}
void USWGHoloCraftingWidget::HandleExperimentResult(ESWGCraftingResult Result)
{
	bExperimentPending = false;
	if (Crafting) { ExperimentAllocations.Init(0, Crafting->GetExperimentGroups().Num()); }
	if (CurrentStageIndex == 1) { RebuildLaterStagePanel(); }
	if (StatusText) { StatusText->SetText(Result == ESWGCraftingResult::CriticalFailure
		? NSLOCTEXT("SWGEmu", "HoloExperimentFailed", "Critical failure")
		: NSLOCTEXT("SWGEmu", "HoloExperimentResult", "Experiment complete")); }
}
void USWGHoloCraftingWidget::HandleSlotResult(ESWGCraftingSlotResult Result)
{
	if (!StatusText) { return; }
	StatusText->SetText(Result == ESWGCraftingSlotResult::OK
		? NSLOCTEXT("SWGEmu", "HoloCraftSlotOK", "Added.")
		: NSLOCTEXT("SWGEmu", "HoloCraftSlotBad", "That doesn't fit there."));
}
void USWGHoloCraftingWidget::HandleSessionClosed() { Dismiss(); }
void USWGHoloCraftingWidget::HandleCreateClicked() { Create(); }
void USWGHoloCraftingWidget::HandleTabClicked() { if (CurrentStageIndex > 0 && CurrentStageIndex < 3) { Crafting->Assemble(); } else { BackToSections(); } }
void USWGHoloCraftingWidget::HandleWindowClicked() { OnSwitchToWindow.Broadcast(); }
void USWGHoloCraftingWidget::HandleCloseClicked() { Close(); }
