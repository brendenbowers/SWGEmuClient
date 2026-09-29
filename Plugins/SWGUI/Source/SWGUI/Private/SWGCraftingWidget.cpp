#include "SWGCraftingWidget.h"
#include "SWGInventoryQuery.h"
#include "SWGRetailStyle.h"
#include "SWGTravelWidget.h"
#include "ModelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Network/Messages/Zone/Object/CraftingMessages.h"
#include "Network/Objects/Zone/Object/CraftingDraftSlot.h"
#include "Objects/Tangible/SWGItem.h"
#include "Subsystems/SWGCraftingSubsystem.h"
#include "Subsystems/SWGExamineSubsystem.h"
#include "Subsystems/SWGObjectGraphSubsystem.h"
#include "Subsystems/SWGTreSubsystem.h"
#include "TRE/SWGResourceClassRow.h"

namespace
{
	/**
	 * Retail's ui_craft_draft.inc TabData is unpopulated placeholder text
	 * (see crafting-protocol.md's "Tool object fields" section) — the real
	 * client fills captions from its own compiled script, which we don't
	 * have. These names are authored to fit each bit's observed tool usage
	 * (bin/scripts/object/tangible/crafting/station/*.lua), not extracted.
	 */
	FText GetTabCaption(int32 TabBit)
	{
		switch (TabBit)
		{
			case 1:        return NSLOCTEXT("SWGEmu", "CraftTab1", "Weapons");
			case 2:        return NSLOCTEXT("SWGEmu", "CraftTab2", "Clothing");
			case 4:        return NSLOCTEXT("SWGEmu", "CraftTab4", "Food");
			case 8:        return NSLOCTEXT("SWGEmu", "CraftTab8", "Armor");
			case 16:       return NSLOCTEXT("SWGEmu", "CraftTab16", "Melee Weapons");
			case 32:       return NSLOCTEXT("SWGEmu", "CraftTab32", "Ranged Weapons");
			case 64:       return NSLOCTEXT("SWGEmu", "CraftTab64", "Chemicals");
			case 128:      return NSLOCTEXT("SWGEmu", "CraftTab128", "Beverages");
			case 256:      return NSLOCTEXT("SWGEmu", "CraftTab256", "Spices");
			case 512:      return NSLOCTEXT("SWGEmu", "CraftTab512", "Structures");
			case 1024:     return NSLOCTEXT("SWGEmu", "CraftTab1024", "Furniture");
			case 2048:     return NSLOCTEXT("SWGEmu", "CraftTab2048", "Lightsabers");
			case 4096:     return NSLOCTEXT("SWGEmu", "CraftTab4096", "Droids");
			case 8192:     return NSLOCTEXT("SWGEmu", "CraftTab8192", "Food (Advanced)");
			case 16384:    return NSLOCTEXT("SWGEmu", "CraftTab16384", "Clothing (Segments)");
			case 32768:    return NSLOCTEXT("SWGEmu", "CraftTab32768", "Clothing (Complete)");
			case 65536:    return NSLOCTEXT("SWGEmu", "CraftTab65536", "Heavy Weapons");
			case 131072:   return NSLOCTEXT("SWGEmu", "CraftTab131072", "Starship Components");
			case 262144:   return NSLOCTEXT("SWGEmu", "CraftTab262144", "Starship Weapons");
			case 524288:   return NSLOCTEXT("SWGEmu", "CraftTab524288", "Munitions");
			case static_cast<int32>(2148007936u): return NSLOCTEXT("SWGEmu", "CraftTabMisc", "Miscellaneous");
			default:       return FText::Format(NSLOCTEXT("SWGEmu", "CraftTabUnknown", "Tab {0}"), FText::AsNumber(TabBit));
		}
	}

	/**
	 * crafting.stf res_* captions, indexed by ResourceWeight.h's property
	 * codes (PO=0 CR=1 CD=2 DR=3 HR=4 FL=5 MA=6 PE=7 OQ=8 SR=9 UT=10 — see
	 * crafting-protocol.md's 0x207 section). PO is filler/none, never a real weight.
	 */
	FText GetResourcePropertyCaption(uint8 Property)
	{
		static const TCHAR* Names[] = { TEXT("—"), TEXT("Cold Resistance"), TEXT("Conductivity"),
			TEXT("Decay Resistance"), TEXT("Heat Resistance"), TEXT("Flavor"), TEXT("Malleability"),
			TEXT("Potential Energy"), TEXT("Overall Quality"), TEXT("Shock Resistance"), TEXT("Unit Toughness") };
		return Property < UE_ARRAY_COUNT(Names) ? FText::FromString(Names[Property]) : FText::AsNumber(Property);
	}

	/**
	 * IngredientSlot::* captions (IngredientSlot.h:36-63) — no STF keys are
	 * documented for these yet (crafting-protocol.md), so these are authored,
	 * not extracted. Only the outcomes reachable from the client UI get a
	 * specific line; the rest fall back to a generic "can't use that" message.
	 */
	FText GetSlotResultCaption(ESWGCraftingSlotResult Result)
	{
		switch (Result)
		{
			case ESWGCraftingSlotResult::OK:                       return NSLOCTEXT("SWGEmu", "CraftSlotOK", "Added.");
			case ESWGCraftingSlotResult::Full:                     return NSLOCTEXT("SWGEmu", "CraftSlotFull", "That slot is already full.");
			case ESWGCraftingSlotResult::InvalidIngredientSize:    return NSLOCTEXT("SWGEmu", "CraftSlotBadSize", "That doesn't fit this slot.");
			case ESWGCraftingSlotResult::InvalidIngredient:        return NSLOCTEXT("SWGEmu", "CraftSlotBadIngredient", "That isn't a valid ingredient for this slot.");
			case ESWGCraftingSlotResult::IngredientNotInInventory: return NSLOCTEXT("SWGEmu", "CraftSlotNotInInventory", "That item isn't in your inventory anymore.");
			case ESWGCraftingSlotResult::BadResourceFor:           return NSLOCTEXT("SWGEmu", "CraftSlotBadResource", "Wrong resource type for this slot.");
			case ESWGCraftingSlotResult::ComponentDamaged:         return NSLOCTEXT("SWGEmu", "CraftSlotDamaged", "That component is too damaged to use.");
			case ESWGCraftingSlotResult::NoInventory:              return NSLOCTEXT("SWGEmu", "CraftSlotNoInventory", "Can't reach your inventory.");
			default:                                               return NSLOCTEXT("SWGEmu", "CraftSlotGeneric", "Can't use that there.");
		}
	}

	/** CraftingManager::* result captions (CraftingManager.idl:37-45) — authored, no documented STF keys yet. */
	FText GetAssemblyResultCaption(ESWGCraftingResult Result)
	{
		switch (Result)
		{
			case ESWGCraftingResult::AmazingSuccess:   return NSLOCTEXT("SWGEmu", "CraftResultAmazing", "Amazing success!");
			case ESWGCraftingResult::GreatSuccess:     return NSLOCTEXT("SWGEmu", "CraftResultGreat", "Great success!");
			case ESWGCraftingResult::GoodSuccess:      return NSLOCTEXT("SWGEmu", "CraftResultGood", "Good success!");
			case ESWGCraftingResult::ModerateSuccess:  return NSLOCTEXT("SWGEmu", "CraftResultModerate", "Moderate success.");
			case ESWGCraftingResult::Success:          return NSLOCTEXT("SWGEmu", "CraftResultSuccess", "Success.");
			case ESWGCraftingResult::MarginalSuccess:  return NSLOCTEXT("SWGEmu", "CraftResultMarginal", "Marginal success.");
			case ESWGCraftingResult::Ok:               return NSLOCTEXT("SWGEmu", "CraftResultOk", "It's assembled.");
			case ESWGCraftingResult::BarelySuccessful: return NSLOCTEXT("SWGEmu", "CraftResultBarely", "Barely successful.");
			case ESWGCraftingResult::CriticalFailure:  return NSLOCTEXT("SWGEmu", "CraftResultCritical", "Critical failure!");
			default:                                   return FText::GetEmpty();
		}
	}

	/**
	 * Walks a resource class's raw ENUM ancestry (metal -> metal_ferrous ->
	 * steel -> steel_duralloy, per SWGResourceClass::DataTablePath) to check
	 * whether Current is RequiredType or a descendant of it — same table and
	 * walk shape as SWGResourceClass::GetIconCandidates and
	 * USWGSurveySubsystem::LoadResourceTree, just comparing raw IDs instead
	 * of collecting display names.
	 */
	bool IsResourceClassOrDescendant(const UDataTable* Table, FString Current, const FString& RequiredType)
	{
		for (int32 Depth = 0; !Current.IsEmpty() && Depth < 16; ++Depth)
		{
			if (Current.Equals(RequiredType, ESearchCase::IgnoreCase))
			{
				return true;
			}
			const FSWGResourceClassRow* Row = Table ? Table->FindRow<FSWGResourceClassRow>(FName(*Current), TEXT("Craft"), false) : nullptr;
			Current = Row ? Row->ParentClass : FString();
		}
		return false;
	}

	FButtonStyle MakeTabStyle(bool bSelected)
	{
		FButtonStyle Style;
		const FLinearColor Idle = bSelected ? FLinearColor::FromSRGBColor(FColor(0x00, 0xD6, 0xFB, 0x70)) : FLinearColor::Transparent;
		Style.SetNormal(FSlateColorBrush(Idle));
		Style.SetHovered(FSlateColorBrush(FLinearColor::FromSRGBColor(FColor(0x00, 0xD6, 0xFB, 0x30))));
		Style.SetPressed(FSlateColorBrush(FLinearColor::FromSRGBColor(FColor(0x00, 0xD6, 0xFB, 0x70))));
		Style.SetDisabled(FSlateColorBrush(FLinearColor::Transparent));
		Style.SetNormalPadding(FMargin(6.f, 2.f));
		Style.SetPressedPadding(FMargin(6.f, 2.f));
		return Style;
	}

	UButton* MakeCraftTile(UWidgetTree* Tree, UVerticalBox*& Contents, bool bSelected)
	{
		UButton* Button = Tree->ConstructWidget<UButton>();
		FButtonStyle Style = MakeTabStyle(bSelected);
		Style.SetNormal(FSlateColorBrush(FLinearColor::FromSRGBColor(bSelected ? FColor(0x15, 0x7F, 0x94, 0xE0) : FColor(0x18, 0x45, 0x53, 0xD0))));
		Button->SetStyle(Style);
		USizeBox* Box = Tree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(116.f);
		Box->SetHeightOverride(124.f);
		Button->AddChild(Box);
		Contents = Tree->ConstructWidget<UVerticalBox>();
		Box->AddChild(Contents);
		return Button;
	}
}

void USWGCraftingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	UGameInstance* GameInstance = GetGameInstance();
	Crafting = GameInstance ? GameInstance->GetSubsystem<USWGCraftingSubsystem>() : nullptr;
	Tre = GameInstance ? GameInstance->GetSubsystem<USWGTreSubsystem>() : nullptr;
	Examine = GameInstance ? GameInstance->GetSubsystem<USWGExamineSubsystem>() : nullptr;
	if (Examine) { Examine->OnExamineInfo.AddUniqueDynamic(this, &USWGCraftingWidget::HandleResourceExamineInfo); }
	SetTitle(NSLOCTEXT("SWGEmu", "CraftingWindowTitle", "Crafting"));

	if (Crafting)
	{
		Crafting->OnSessionStarted.AddUniqueDynamic(this, &USWGCraftingWidget::HandleSessionStarted);
		Crafting->OnStageChanged.AddUniqueDynamic(this, &USWGCraftingWidget::HandleStageChanged);
		Crafting->OnSlotsChanged.AddUniqueDynamic(this, &USWGCraftingWidget::HandleSlotsChanged);
		Crafting->OnSlotResult.AddUniqueDynamic(this, &USWGCraftingWidget::HandleSlotResult);
		Crafting->OnAssemblyResult.AddUniqueDynamic(this, &USWGCraftingWidget::HandleAssemblyResult);
		Crafting->OnExperimentResult.AddUniqueDynamic(this, &USWGCraftingWidget::HandleExperimentResult);
		Crafting->OnSessionClosed.AddUniqueDynamic(this, &USWGCraftingWidget::HandleSessionClosed);
		Crafting->OnDraftPreviewChanged.AddUniqueDynamic(this, &USWGCraftingWidget::HandleDraftPreviewChanged);
	}
	SelectButton->OnClicked.AddUniqueDynamic(this, &USWGCraftingWidget::HandleSelectClicked);
	AssembleButton->OnClicked.AddUniqueDynamic(this, &USWGCraftingWidget::HandleAssembleClicked);
	if (UVerticalBox* ContentColumn = Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("Content"))))
	{
		// Persistent across every post-Draft page — the prototype has a live
		// object id from Assembling onward (discovered via UpdateContainmentMessage,
		// see USWGCraftingSubsystem), so this can show it regardless of which
		// sub-page (Assembly/Experiment/Customize/Summary) is active.
		PrototypeModelView = WidgetTree->ConstructWidget<UModelWidget>();
		PrototypeModelView->DesiredSize = FVector2D(96.f, 96.f);
		PrototypeModelView->RotateSpeed = 20.f;
		if (UVerticalBoxSlot* ModelSlot = ContentColumn->AddChildToVerticalBox(PrototypeModelView))
		{
			ModelSlot->SetHorizontalAlignment(HAlign_Center);
		}

		ExperimentBody = WidgetTree->ConstructWidget<UVerticalBox>();
		ContentColumn->AddChildToVerticalBox(ExperimentBody)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UTextBlock* Stage = WidgetTree->ConstructWidget<UTextBlock>();
		Stage->SetText(NSLOCTEXT("SWGEmu", "CraftExperimentStage", "Experiment  •  Stage 3 of 6"));
		Stage->SetFont(SWGRetailStyle::Font(13));
		ExperimentBody->AddChild(Stage);
		ExperimentPointsText = WidgetTree->ConstructWidget<UTextBlock>();
		ExperimentPointsText->SetFont(SWGRetailStyle::Font(12));
		ExperimentBody->AddChild(ExperimentPointsText);
		ExperimentRowsList = WidgetTree->ConstructWidget<UScrollBox>();
		ExperimentBody->AddChildToVerticalBox(ExperimentRowsList)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UHorizontalBox* ExperimentActions = WidgetTree->ConstructWidget<UHorizontalBox>();
		ExperimentBody->AddChild(ExperimentActions);
		const auto AddAction = [this, ExperimentActions](const FText& Label)
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>();
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
			Text->SetText(Label);
			Button->AddChild(Text);
			ExperimentActions->AddChild(Button);
			if (UObject* Tint = SWGRetailStyle::ApplyHudButton(Button, Tre)) { ButtonTextTints.Add(Tint); }
			return Button;
		};
		ExperimentButton = AddAction(NSLOCTEXT("SWGEmu", "CraftExperimentAction", "Experiment"));
		ExperimentContinueButton = AddAction(NSLOCTEXT("SWGEmu", "CraftExperimentContinue", "Continue"));
		ExperimentButton->OnClicked.AddUniqueDynamic(this, &USWGCraftingWidget::HandleExperimentClicked);
		ExperimentContinueButton->OnClicked.AddUniqueDynamic(this, &USWGCraftingWidget::HandleExperimentContinueClicked);

		// ── Customize page ──────────────────────────────────────────────
		CustomizeBody = WidgetTree->ConstructWidget<UVerticalBox>();
		ContentColumn->AddChildToVerticalBox(CustomizeBody)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UTextBlock* CustomizeStage = WidgetTree->ConstructWidget<UTextBlock>();
		CustomizeStage->SetText(NSLOCTEXT("SWGEmu", "CraftCustomizeStage", "Customize  •  Stage 4 of 6"));
		CustomizeStage->SetFont(SWGRetailStyle::Font(13));
		CustomizeBody->AddChild(CustomizeStage);
		CustomizeNameBox = WidgetTree->ConstructWidget<UEditableTextBox>();
		CustomizeNameBox->SetHintText(NSLOCTEXT("SWGEmu", "CraftCustomizeNameHint", "Item name (blank keeps the default)"));
		CustomizeBody->AddChild(CustomizeNameBox);
		UTextBlock* TemplateLabel = WidgetTree->ConstructWidget<UTextBlock>();
		TemplateLabel->SetText(NSLOCTEXT("SWGEmu", "CraftCustomizeTemplateLabel", "Appearance"));
		TemplateLabel->SetFont(SWGRetailStyle::Font(11));
		CustomizeBody->AddChild(TemplateLabel);
		CustomizeTemplateList = WidgetTree->ConstructWidget<UHorizontalBox>();
		CustomizeBody->AddChild(CustomizeTemplateList);
		UTextBlock* VarsLabel = WidgetTree->ConstructWidget<UTextBlock>();
		VarsLabel->SetText(NSLOCTEXT("SWGEmu", "CraftCustomizeVarsLabel", "Colours"));
		VarsLabel->SetFont(SWGRetailStyle::Font(11));
		CustomizeBody->AddChild(VarsLabel);
		CustomizeVarsList = WidgetTree->ConstructWidget<UScrollBox>();
		CustomizeBody->AddChildToVerticalBox(CustomizeVarsList)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UHorizontalBox* CustomizeActions = WidgetTree->ConstructWidget<UHorizontalBox>();
		CustomizeBody->AddChild(CustomizeActions);
		const auto AddCustomizeAction = [this, CustomizeActions](const FText& Label)
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>();
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
			Text->SetText(Label);
			Button->AddChild(Text);
			CustomizeActions->AddChild(Button);
			if (UObject* Tint = SWGRetailStyle::ApplyHudButton(Button, Tre)) { ButtonTextTints.Add(Tint); }
			return Button;
		};
		CustomizeApplyButton = AddCustomizeAction(NSLOCTEXT("SWGEmu", "CraftCustomizeApply", "Apply"));
		CustomizeContinueButton = AddCustomizeAction(NSLOCTEXT("SWGEmu", "CraftCustomizeContinue", "Continue"));
		CustomizeApplyButton->OnClicked.AddUniqueDynamic(this, &USWGCraftingWidget::HandleCustomizeApplyClicked);
		CustomizeContinueButton->OnClicked.AddUniqueDynamic(this, &USWGCraftingWidget::HandleCustomizeContinueClicked);

		// ── Summary page ────────────────────────────────────────────────
		SummaryBody = WidgetTree->ConstructWidget<UVerticalBox>();
		ContentColumn->AddChildToVerticalBox(SummaryBody)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UTextBlock* SummaryStage = WidgetTree->ConstructWidget<UTextBlock>();
		SummaryStage->SetText(NSLOCTEXT("SWGEmu", "CraftSummaryStage", "Summary  •  Stage 5 of 6"));
		SummaryStage->SetFont(SWGRetailStyle::Font(13));
		SummaryBody->AddChild(SummaryStage);
		SummaryNameText = WidgetTree->ConstructWidget<UTextBlock>();
		SummaryNameText->SetFont(SWGRetailStyle::Font(14));
		SummaryBody->AddChild(SummaryNameText);
		UHorizontalBox* SummaryActions = WidgetTree->ConstructWidget<UHorizontalBox>();
		SummaryBody->AddChild(SummaryActions);
		const auto AddSummaryAction = [this, SummaryActions](const FText& Label)
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>();
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
			Text->SetText(Label);
			Button->AddChild(Text);
			SummaryActions->AddChild(Button);
			if (UObject* Tint = SWGRetailStyle::ApplyHudButton(Button, Tre)) { ButtonTextTints.Add(Tint); }
			return Button;
		};
		SummaryPracticeButton = AddSummaryAction(NSLOCTEXT("SWGEmu", "CraftSummaryPractice", "Practice"));
		SummaryCreateButton = AddSummaryAction(NSLOCTEXT("SWGEmu", "CraftSummaryCreate", "Create Prototype"));
		SummarySchematicButton = AddSummaryAction(NSLOCTEXT("SWGEmu", "CraftSummarySchematic", "Create Schematic"));
		SummaryRetrieveButton = AddSummaryAction(NSLOCTEXT("SWGEmu", "CraftSummaryRetrieve", "Retrieve Output"));
		SummaryPracticeButton->OnClicked.AddUniqueDynamic(this, &USWGCraftingWidget::HandleSummaryPracticeClicked);
		SummaryCreateButton->OnClicked.AddUniqueDynamic(this, &USWGCraftingWidget::HandleSummaryCreateClicked);
		SummarySchematicButton->OnClicked.AddUniqueDynamic(this, &USWGCraftingWidget::HandleSummarySchematicClicked);
		SummaryRetrieveButton->OnClicked.AddUniqueDynamic(this, &USWGCraftingWidget::HandleSummaryRetrieveClicked);
	}
	if (ButtonTextTints.IsEmpty())
	{
		if (UObject* TextTint = SWGRetailStyle::ApplyHudButton(SelectButton, Tre, TEXT("@ui:create")))
		{
			ButtonTextTints.Add(TextTint);
		}
		// No confirmed retail STF key for this one (crafting-protocol.md's key survey didn't cover it) — keep the Blueprint's authored "Assemble" text.
		if (UObject* TextTint = SWGRetailStyle::ApplyHudButton(AssembleButton, Tre))
		{
			ButtonTextTints.Add(TextTint);
		}
	}

	RebuildTabs();
	RebuildSchematicList();
	RefreshDetails();
	RebuildAssemblySlots();
	RebuildExperimentRows();
	RebuildCustomizePage();
	RefreshSummaryPage();
	RefreshPageVisibility();
	RefreshStatus();
}

void USWGCraftingWidget::NativeDestruct()
{
	if (Examine) { Examine->OnExamineInfo.RemoveDynamic(this, &USWGCraftingWidget::HandleResourceExamineInfo); }
	if (Crafting)
	{
		Crafting->OnSessionStarted.RemoveAll(this);
		Crafting->OnStageChanged.RemoveAll(this);
		Crafting->OnSlotsChanged.RemoveAll(this);
		Crafting->OnSlotResult.RemoveAll(this);
		Crafting->OnAssemblyResult.RemoveAll(this);
		Crafting->OnExperimentResult.RemoveAll(this);
		Crafting->OnSessionClosed.RemoveAll(this);
		Crafting->OnDraftPreviewChanged.RemoveAll(this);
	}
	Super::NativeDestruct();
}

void USWGCraftingWidget::RebuildTabs()
{
	TabBar->ClearChildren();
	TabButtons.Reset();
	VisibleTabs.Reset();
	if (!Crafting)
	{
		return;
	}
	for (const FSWGCraftingSchematicOption& Option : Crafting->GetSchematics())
	{
		VisibleTabs.AddUnique(Option.ToolTab);
	}
	if (SelectedTab != 0 && !VisibleTabs.Contains(SelectedTab))
	{
		SelectedTab = 0;
	}

	auto MakeTabButton = [this](const FText& Label, int32 TabValue)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		Button->SetStyle(MakeTabStyle(TabValue == SelectedTab));
		UTextBlock* ButtonLabel = WidgetTree->ConstructWidget<UTextBlock>();
		ButtonLabel->SetText(Label);
		ButtonLabel->SetFont(SWGRetailStyle::Font(12, false));
		Button->AddChild(ButtonLabel);
		TabBar->AddChild(Button);

		USWGTravelPlanetClickForwarder* Forwarder = NewObject<USWGTravelPlanetClickForwarder>(this);
		Forwarder->Action = [this, TabValue]()
		{
			SelectedTab = TabValue;
			HighlightedIndex = INDEX_NONE;
			RebuildTabs();
			RebuildSchematicList();
			RefreshDetails();
		};
		Button->OnClicked.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleClicked);
		ClickForwarders.Add(Forwarder);
		TabButtons.Add(Button);
	};

	if (VisibleTabs.Num() > 1)
	{
		MakeTabButton(NSLOCTEXT("SWGEmu", "CraftTabAll", "All"), 0);
	}
	for (const int32 TabValue : VisibleTabs)
	{
		MakeTabButton(GetTabCaption(TabValue), TabValue);
	}
}

void USWGCraftingWidget::RebuildSchematicList()
{
	SchematicList->ClearChildren();
	SchematicButtons.Reset();
	SchematicForwarders.Reset();
	if (!Crafting)
	{
		return;
	}

	const TArray<FSWGCraftingSchematicOption>& Schematics = Crafting->GetSchematics();
	TArray<int32> VisibleIndices;
	for (int32 Index = 0; Index < Schematics.Num(); ++Index)
	{
		if (SelectedTab == 0 || Schematics[Index].ToolTab == SelectedTab)
		{
			VisibleIndices.Add(Index);
		}
	}
	VisibleIndices.Sort([&Schematics](int32 A, int32 B)
	{
		return Schematics[A].TemplatePath < Schematics[B].TemplatePath;
	});
	TSet<FString> SeenCategories;
	for (const int32 Index : VisibleIndices)
	{
		const FSWGCraftingSchematicOption& Option = Schematics[Index];
		FString Folder = FPaths::GetPath(Option.TemplatePath);
		Folder.RemoveFromStart(TEXT("object/draft_schematic/"));
		TArray<FString> Parts;
		Folder.ParseIntoArray(Parts, TEXT("/"), true);
		if (Parts.IsEmpty()) { Parts.Add(TEXT("other")); }
		FString CategoryKey;
		bool bHidden = false;
		for (int32 Depth = 0; Depth < Parts.Num(); ++Depth)
		{
			CategoryKey = CategoryKey.IsEmpty() ? Parts[Depth] : CategoryKey + TEXT("/") + Parts[Depth];
			if (!SeenCategories.Contains(CategoryKey))
			{
				SeenCategories.Add(CategoryKey);
				UButton* Category = WidgetTree->ConstructWidget<UButton>();
				Category->SetStyle(MakeTabStyle(false));
				UTextBlock* CategoryLabel = WidgetTree->ConstructWidget<UTextBlock>();
				FString Name = Parts[Depth];
				Name.ReplaceInline(TEXT("_"), TEXT(" "));
				if (!Name.IsEmpty()) { Name[0] = FChar::ToUpper(Name[0]); }
				CategoryLabel->SetText(FText::FromString(FString::Printf(TEXT("%s %s"), CollapsedSchematicNodes.Contains(CategoryKey) ? TEXT("[+]") : TEXT("[-]"), *Name)));
				CategoryLabel->SetFont(SWGRetailStyle::Font(12));
				if (UButtonSlot* CaptionSlot = Cast<UButtonSlot>(Category->AddChild(CategoryLabel)))
				{
					CaptionSlot->SetPadding(FMargin(Depth * 14.f, 2.f, 0.f, 2.f));
					CaptionSlot->SetHorizontalAlignment(HAlign_Left);
				}
				SchematicList->AddChild(Category);
				USWGTravelPlanetClickForwarder* Forwarder = NewObject<USWGTravelPlanetClickForwarder>(this);
				const FString Key = CategoryKey;
				Forwarder->Action = [this, Key]()
				{
					if (CollapsedSchematicNodes.Contains(Key)) { CollapsedSchematicNodes.Remove(Key); }
					else { CollapsedSchematicNodes.Add(Key); }
					RebuildSchematicList();
				};
				Category->OnClicked.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleClicked);
				SchematicForwarders.Add(Forwarder);
			}
			if (CollapsedSchematicNodes.Contains(CategoryKey)) { bHidden = true; break; }
		}
		if (bHidden) { continue; }
		UButton* Row = WidgetTree->ConstructWidget<UButton>();
		Row->SetStyle(MakeTabStyle(Index == HighlightedIndex));
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		FString DisplayName = Option.Name;
		DisplayName.RemoveFromStart(TEXT("shared "));
		DisplayName.RemoveFromStart(Parts.Last() + TEXT(" "));
		if (!DisplayName.IsEmpty()) { DisplayName[0] = FChar::ToUpper(DisplayName[0]); }
		Label->SetText(FText::FromString(DisplayName));
		Label->SetFont(SWGRetailStyle::Font(12, false));
		if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(Row->AddChild(Label)))
		{
			LabelSlot->SetPadding(FMargin(Parts.Num() * 14.f, 2.f, 0.f, 2.f));
			LabelSlot->SetHorizontalAlignment(HAlign_Left);
		}
		SchematicList->AddChild(Row);

		USWGTravelPlanetClickForwarder* Forwarder = NewObject<USWGTravelPlanetClickForwarder>(this);
		Forwarder->Action = [this, Index]()
		{
			HighlightedIndex = Index;
			Crafting->RequestDraftPreview({Crafting->GetSchematics()[Index].SchematicCrc});
			RebuildSchematicList();
			RefreshDetails();
		};
		Row->OnClicked.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleClicked);
		SchematicForwarders.Add(Forwarder);
		SchematicButtons.Add(Row);
	}
}

void USWGCraftingWidget::RefreshDetails()
{
	SlotsList->ClearChildren();
	WeightsList->ClearChildren();
	if (SchematicNameText)
	{
		SchematicNameText->SetText(FText::GetEmpty());
	}
	SelectButton->SetIsEnabled(false);
	if (!Crafting || !Crafting->GetSchematics().IsValidIndex(HighlightedIndex))
	{
		return;
	}
	const FSWGCraftingSchematicOption& Option = Crafting->GetSchematics()[HighlightedIndex];
	if (SchematicNameText)
	{
		FString Name = Option.Name;
		Name.RemoveFromStart(TEXT("shared "));
		if (!Name.IsEmpty()) { Name[0] = FChar::ToUpper(Name[0]); }
		SchematicNameText->SetText(FText::FromString(Name));
	}
	SelectButton->SetIsEnabled(true);
	FString DescriptionTable, DescriptionKey;
	if (Tre && Tre->FindTemplateStringId(Option.TemplatePath, TEXT("detailedDescription"), DescriptionTable, DescriptionKey))
	{
		const FString Description = Tre->LookupString(DescriptionTable, DescriptionKey);
		if (!Description.IsEmpty())
		{
			UTextBlock* DescriptionText = WidgetTree->ConstructWidget<UTextBlock>();
			DescriptionText->SetText(FText::FromString(Description));
			DescriptionText->SetFont(SWGRetailStyle::Font(10, false));
			DescriptionText->SetAutoWrapText(true);
			SlotsList->AddChild(DescriptionText);
		}
	}

	if (const FSWGCraftingDraftSlotsIn* Slots = Crafting->GetDraftSlotsPreview(Option.SchematicCrc))
	{
		UTextBlock* Summary = WidgetTree->ConstructWidget<UTextBlock>();
		Summary->SetText(FText::Format(NSLOCTEXT("SWGEmu", "CraftDraftSummary", "Complexity {0}  •  {1} ingredients"),
			FText::AsNumber(Slots->Complexity), FText::AsNumber(Slots->Slots.Num())));
		Summary->SetFont(SWGRetailStyle::Font(11));
		SlotsList->AddChild(Summary);
		for (const FSWGDraftSlot& SlotEntry : Slots->Slots)
		{
			UTextBlock* Row = WidgetTree->ConstructWidget<UTextBlock>();
			FString Name = Tre ? Tre->LookupString(SlotEntry.StringIdFile, SlotEntry.StringIdName) : FString();
			if (Name.IsEmpty()) { Name = SlotEntry.StringIdName; Name.ReplaceInline(TEXT("_"), TEXT(" ")); }
			FString Type = SlotEntry.ResourceType.IsEmpty()
				? (SlotEntry.Kind == ESWGDraftSlotKind::Identical ? TEXT("Identical item") : TEXT("Component"))
				: SlotEntry.ResourceType;
			Type.ReplaceInline(TEXT("_"), TEXT(" "));
			Row->SetText(FText::FromString(FString::Printf(TEXT("%s%s\n  %d × %s"), *Name,
				SlotEntry.bOptional ? TEXT(" (optional)") : TEXT(""), SlotEntry.Quantity, *Type)));
			Row->SetFont(SWGRetailStyle::Font(11, false));
			Row->SetAutoWrapText(true);
			SlotsList->AddChild(Row);
		}
	}
	else
	{
		UTextBlock* Loading = WidgetTree->ConstructWidget<UTextBlock>();
		Loading->SetText(NSLOCTEXT("SWGEmu", "CraftSlotsLoading", "Loading..."));
		Loading->SetFont(SWGRetailStyle::Font(11, false));
		SlotsList->AddChild(Loading);
	}

	if (const FSWGCraftingResourceWeightsIn* Weights = Crafting->GetResourceWeightsPreview(Option.SchematicCrc))
	{
		// Weights[i] is the i-th resource-accepting draft slot's own weighted
		// properties; pair it with the corresponding draft slot for its label.
		for (int32 SlotIndex = 0; SlotIndex < Weights->Weights.Num(); ++SlotIndex)
		{
			UTextBlock* Header = WidgetTree->ConstructWidget<UTextBlock>();
			FString SlotName = FString::Printf(TEXT("Resource slot %d"), SlotIndex + 1);
			if (const FSWGCraftingDraftSlotsIn* Slots = Crafting->GetDraftSlotsPreview(Option.SchematicCrc))
			{
				int32 ResourceIndex = 0;
				for (const FSWGDraftSlot& DraftSlot : Slots->Slots)
				{
					if (DraftSlot.Kind != ESWGDraftSlotKind::Resource) { continue; }
					if (ResourceIndex++ == SlotIndex)
					{
						SlotName = Tre ? Tre->LookupString(DraftSlot.StringIdFile, DraftSlot.StringIdName) : FString();
						if (SlotName.IsEmpty()) { SlotName = DraftSlot.StringIdName; SlotName.ReplaceInline(TEXT("_"), TEXT(" ")); }
						break;
					}
				}
			}
			Header->SetText(FText::FromString(SlotName));
			Header->SetFont(SWGRetailStyle::Font(11));
			WeightsList->AddChild(Header);
			for (const FSWGResourceWeightEntry& Entry : Weights->Weights[SlotIndex].Entries)
			{
				UTextBlock* Row = WidgetTree->ConstructWidget<UTextBlock>();
				Row->SetText(FText::Format(NSLOCTEXT("SWGEmu", "CraftWeightRow", "  {0}: {1}"),
					GetResourcePropertyCaption(Entry.Property), FText::AsNumber(Entry.Weight)));
				Row->SetFont(SWGRetailStyle::Font(11, false));
				WeightsList->AddChild(Row);
			}
		}
	}
}

void USWGCraftingWidget::RefreshStatus()
{
	if (!StatusText || !Crafting)
	{
		return;
	}
	if (!TransientStatus.IsEmpty())
	{
		StatusText->SetText(TransientStatus);
		return;
	}
	switch (Crafting->GetState())
	{
		case ESWGCraftingSessionState::ChoosingSchematic:
			StatusText->SetText(NSLOCTEXT("SWGEmu", "CraftStatusChoose", "Select a schematic, then press Create."));
			break;
		case ESWGCraftingSessionState::Assembling:
			StatusText->SetText(Crafting->IsAssemblyReady()
				? NSLOCTEXT("SWGEmu", "CraftStatusReadyAssemble", "All slots filled — press Assemble.")
				: NSLOCTEXT("SWGEmu", "CraftStatusAssembling", "Fill the required slots, then press Assemble."));
			break;
		case ESWGCraftingSessionState::Experimenting:
			StatusText->SetText(NSLOCTEXT("SWGEmu", "CraftStatusExperimenting", "Allocate points, then experiment or continue."));
			break;
		case ESWGCraftingSessionState::Assembled:
			StatusText->SetText(NSLOCTEXT("SWGEmu", "CraftStatusAssembled", "Assembled — customize it, or press Continue to finish."));
			break;
		case ESWGCraftingSessionState::Customizing:
			StatusText->SetText(NSLOCTEXT("SWGEmu", "CraftStatusCustomizing", "Customized — press Continue to finish."));
			break;
		case ESWGCraftingSessionState::ReadyToFinish:
			StatusText->SetText(NSLOCTEXT("SWGEmu", "CraftStatusReady", "Ready — Practice, Create Prototype, or Create Schematic."));
			break;
		default:
			StatusText->SetText(FText::GetEmpty());
			break;
	}
}

void USWGCraftingWidget::RefreshPageVisibility()
{
	if (!Crafting)
	{
		return;
	}
	const ESWGCraftingSessionState State = Crafting->GetState();
	const bool bDraft = State == ESWGCraftingSessionState::ChoosingSchematic || State == ESWGCraftingSessionState::None;
	const bool bAssembling = State == ESWGCraftingSessionState::Assembling;
	const bool bExperiment = State == ESWGCraftingSessionState::Experimenting;
	const bool bCustomize = State == ESWGCraftingSessionState::Assembled || State == ESWGCraftingSessionState::Customizing;
	const bool bSummary = State == ESWGCraftingSessionState::ReadyToFinish;
	Body->SetVisibility(bDraft ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	AssemblyBody->SetVisibility(bAssembling ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (ExperimentBody) { ExperimentBody->SetVisibility(bExperiment ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (CustomizeBody) { CustomizeBody->SetVisibility(bCustomize ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (SummaryBody) { SummaryBody->SetVisibility(bSummary ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	TabBar->SetVisibility(bDraft ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	SelectButton->SetVisibility(bDraft ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	AssembleButton->SetVisibility(bAssembling ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (PrototypeModelView) { PrototypeModelView->SetVisibility(bDraft ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible); }
	if (bCustomize) { RebuildCustomizePage(); }
	if (bSummary) { RefreshSummaryPage(); }
	RefreshPrototypeModel();
}

void USWGCraftingWidget::RefreshPrototypeModel()
{
	if (!PrototypeModelView || !Crafting)
	{
		return;
	}
	const int64 PrototypeId = Crafting->GetPrototypeId();
	if (PrototypeId != 0)
	{
		PrototypeModelView->SetObject(PrototypeId);
	}
	else
	{
		PrototypeModelView->ClearModel();
	}
}

void USWGCraftingWidget::RebuildExperimentRows()
{
	if (!Crafting || !ExperimentRowsList || !ExperimentPointsText || !ExperimentButton)
	{
		return;
	}
	ExperimentRowsList->ClearChildren();
	ExperimentRowForwarders.Reset();
	const TArray<FSWGCraftingExperimentGroup>& Groups = Crafting->GetExperimentGroups();
	ExperimentAllocations.SetNum(Groups.Num());
	int32 Allocated = 0;
	for (const int32 Points : ExperimentAllocations) { Allocated += Points; }
	const int32 Available = FMath::Max(0, Crafting->GetExperimentPointsRemaining() - Allocated);
	ExperimentPointsText->SetText(FText::Format(NSLOCTEXT("SWGEmu", "CraftExperimentPool", "Points available: {0} / {1}    Failure rate: {2}%"),
		FText::AsNumber(Available), FText::AsNumber(Crafting->GetExperimentPointsTotal()),
		FText::AsNumber(FMath::RoundToInt(Crafting->GetFailureRate()))));
	ExperimentButton->SetIsEnabled(Allocated > 0 && !bExperimentPending);

	for (int32 Index = 0; Index < Groups.Num(); ++Index)
	{
		const FSWGCraftingExperimentGroup& Group = Groups[Index];
		UVerticalBox* Row = WidgetTree->ConstructWidget<UVerticalBox>();
		FString Name = Group.Title;
		FText GroupTitle;
		if (Tre) { GroupTitle = FText::FromString(Tre->LookupString(TEXT("crafting"), Name)); }
		if (GroupTitle.IsEmpty())
		{
			Name.RemoveFromStart(TEXT("exp_"));
			Name.ReplaceInline(TEXT("_"), TEXT(" "));
			GroupTitle = FText::FromString(Name);
		}
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::Format(NSLOCTEXT("SWGEmu", "CraftExperimentRow", "{0}: {1}% / {2}%    +{3} points"), GroupTitle,
			FText::AsNumber(FMath::RoundToInt(Group.CurrentPercent * 100.f)),
			FText::AsNumber(FMath::RoundToInt(Group.MaxPercent * 100.f)), FText::AsNumber(ExperimentAllocations[Index])));
		Label->SetFont(SWGRetailStyle::Font(11));
		Row->AddChild(Label);
		UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>();
		Bar->SetPercent(FMath::Clamp(Group.CurrentPercent, 0.f, 1.f));
		Row->AddChild(Bar);
		UHorizontalBox* Controls = WidgetTree->ConstructWidget<UHorizontalBox>();
		Row->AddChild(Controls);
		for (const int32 Delta : {-1, 1})
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>();
			UTextBlock* Glyph = WidgetTree->ConstructWidget<UTextBlock>();
			Glyph->SetText(FText::FromString(Delta > 0 ? TEXT(" + ") : TEXT(" − ")));
			Button->AddChild(Glyph);
			Button->SetIsEnabled(!bExperimentPending && (Delta > 0 ? Available > 0 : ExperimentAllocations[Index] > 0));
			Controls->AddChild(Button);
			USWGTravelPlanetClickForwarder* Forwarder = NewObject<USWGTravelPlanetClickForwarder>(this);
			Forwarder->Action = [this, Index, Delta]()
			{
				ExperimentAllocations[Index] += Delta;
				RebuildExperimentRows();
			};
			Button->OnClicked.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleClicked);
			ExperimentRowForwarders.Add(Forwarder);
		}
		ExperimentRowsList->AddChild(Row);
	}
}

void USWGCraftingWidget::RebuildCustomizePage()
{
	if (!Crafting || !CustomizeTemplateList || !CustomizeVarsList)
	{
		return;
	}

	const TArray<FString>& Templates = Crafting->GetTemplateChoices();
	if (SelectedTemplateChoice != INDEX_NONE && !Templates.IsValidIndex(SelectedTemplateChoice))
	{
		SelectedTemplateChoice = INDEX_NONE;
	}
	CustomizeTemplateList->ClearChildren();
	for (int32 Index = 0; Index < Templates.Num(); ++Index)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		Button->SetStyle(MakeTabStyle(Index == SelectedTemplateChoice));
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		FString Name = Templates[Index];
		Name = FPaths::GetBaseFilename(Name).IsEmpty() ? Name : FPaths::GetBaseFilename(Name);
		Label->SetText(FText::FromString(Name));
		Label->SetFont(SWGRetailStyle::Font(11, false));
		Button->AddChild(Label);
		CustomizeTemplateList->AddChild(Button);

		USWGTravelPlanetClickForwarder* Forwarder = NewObject<USWGTravelPlanetClickForwarder>(this);
		Forwarder->Action = [this, Index]()
		{
			SelectedTemplateChoice = Index;
			RebuildCustomizePage();
		};
		Button->OnClicked.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleClicked);
		CustomizeForwarders.Add(Forwarder);
	}

	const TArray<FString>& VarNames = Crafting->GetCustomizationVarNames();
	const TArray<int32>& VarDefaults = Crafting->GetCustomizationVarDefaults();
	const TArray<int32>& PaletteCounts = Crafting->GetCustomizationPaletteCounts();
	if (CustomizeVarValues.Num() != VarNames.Num())
	{
		CustomizeVarValues = VarDefaults;
		CustomizeVarValues.SetNum(VarNames.Num());
	}
	CustomizeVarsList->ClearChildren();
	for (int32 Index = 0; Index < VarNames.Num(); ++Index)
	{
		const int32 PaletteCount = PaletteCounts.IsValidIndex(Index) ? PaletteCounts[Index] : 0;
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::Format(NSLOCTEXT("SWGEmu", "CraftCustomizeVarRow", "{0}: {1}"),
			FText::FromString(VarNames[Index]), FText::AsNumber(CustomizeVarValues[Index])));
		Label->SetFont(SWGRetailStyle::Font(11, false));
		Row->AddChild(Label);
		for (const int32 Delta : {-1, 1})
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>();
			UTextBlock* Glyph = WidgetTree->ConstructWidget<UTextBlock>();
			Glyph->SetText(FText::FromString(Delta > 0 ? TEXT(" + ") : TEXT(" − ")));
			Button->AddChild(Glyph);
			Row->AddChild(Button);
			USWGTravelPlanetClickForwarder* Forwarder = NewObject<USWGTravelPlanetClickForwarder>(this);
			Forwarder->Action = [this, Index, Delta, PaletteCount]()
			{
				const int32 UpperBound = PaletteCount > 0 ? PaletteCount - 1 : 100;
				CustomizeVarValues[Index] = FMath::Clamp(CustomizeVarValues[Index] + Delta, 0, UpperBound);
				RebuildCustomizePage();
			};
			Button->OnClicked.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleClicked);
			CustomizeForwarders.Add(Forwarder);
		}
		CustomizeVarsList->AddChild(Row);
	}
}

void USWGCraftingWidget::RefreshSummaryPage()
{
	if (!Crafting || !SummaryNameText)
	{
		return;
	}
	SummaryNameText->SetText(FText::FromString(Crafting->GetPrototypeName()));
	if (SummarySchematicButton)
	{
		SummarySchematicButton->SetIsEnabled(Crafting->GetAllowFactoryRun());
	}
}

void USWGCraftingWidget::RebuildAssemblySlots()
{
	AssemblySlotsList->ClearChildren();
	AssemblySlotButtons.Reset();
	if (!Crafting)
	{
		return;
	}
	const TArray<FSWGCraftingSlot>& Slots = Crafting->GetSlots();
	if (FocusedSlotIndex != INDEX_NONE && !Slots.IsValidIndex(FocusedSlotIndex))
	{
		FocusedSlotIndex = INDEX_NONE;
	}
	UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>();
	Grid->SetSlotPadding(FMargin(4.f));
	AssemblySlotsList->AddChild(Grid);
	TArray<FSWGInventoryEntry> Equipped;
	TArray<FSWGInventoryEntry> Contents;
	SWGInventoryQuery::Gather(GetGameInstance(), Equipped, Contents);
	USWGObjectGraphSubsystem* ObjectGraph = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	const UDataTable* ResourceClasses = LoadObject<UDataTable>(nullptr, *SWGResourceClass::DataTablePath);
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FSWGCraftingSlot& SlotEntry = Slots[Index];
		UVerticalBox* TileContent = nullptr;
		UButton* Tile = MakeCraftTile(WidgetTree, TileContent, Index == FocusedSlotIndex);
		int64 PreviewId = SlotEntry.FilledObjectIds.IsEmpty() ? 0 : SlotEntry.FilledObjectIds[0];
		if (PreviewId != 0 && ObjectGraph)
		{
			const int64 SourceId = SelectedResourceSourceIds.FindRef(Index);
			if (SourceId != 0 && ObjectGraph->FindActor(SourceId)) { PreviewId = SourceId; }
		}
		if (PreviewId == 0 && SlotEntry.Kind == ESWGDraftSlotKind::Resource)
		{
			for (const FSWGInventoryEntry& Entry : Contents)
			{
				const ASWGItem* Item = ObjectGraph ? Cast<ASWGItem>(ObjectGraph->FindActor(Entry.ObjectId)) : nullptr;
				if (Item && IsResourceClassOrDescendant(ResourceClasses, Item->ResourceType, SlotEntry.ResourceType))
				{
					PreviewId = Entry.ObjectId;
					break;
				}
			}
		}
		if (PreviewId != 0)
		{
			UModelWidget* SlotModel = WidgetTree->ConstructWidget<UModelWidget>();
			SlotModel->DesiredSize = FVector2D(58.f, 58.f);
			SlotModel->SetObject(PreviewId);
			SlotModel->SetRenderOpacity(SlotEntry.FilledObjectIds.IsEmpty() ? 0.5f : 1.f);
			TileContent->AddChildToVerticalBox(SlotModel)->SetHorizontalAlignment(HAlign_Center);
		}
		else
		{
			UTextBlock* Placeholder = WidgetTree->ConstructWidget<UTextBlock>();
			Placeholder->SetText(FText::FromString(SlotEntry.ResourceType.IsEmpty() ? TEXT("◇") : SlotEntry.ResourceType.Left(10)));
			Placeholder->SetFont(SWGRetailStyle::Font(14));
			TileContent->AddChildToVerticalBox(Placeholder)->SetHorizontalAlignment(HAlign_Center);
		}
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(SlotEntry.Name));
		Label->SetFont(SWGRetailStyle::Font(10, false));
		Label->SetAutoWrapText(true);
		Label->SetColorAndOpacity(FSlateColor(SlotEntry.IsFull()
			? FLinearColor::FromSRGBColor(FColor(0x60, 0xFF, 0x60))
			: FLinearColor::FromSRGBColor(FColor(0x96, 0xF4, 0xFC))));
		TileContent->AddChild(Label);
		UTextBlock* Requirement = WidgetTree->ConstructWidget<UTextBlock>();
		FString Type = SlotEntry.ResourceType;
		if (Type.IsEmpty()) { Type = SlotEntry.Kind == ESWGDraftSlotKind::Identical ? TEXT("Identical item") : TEXT("Component"); }
		Type.ReplaceInline(TEXT("_"), TEXT(" "));
		Requirement->SetText(FText::Format(NSLOCTEXT("SWGEmu", "CraftAssemblyTileRequirement", "{0}  {1}/{2}"),
			FText::FromString(Type), FText::AsNumber(SlotEntry.FilledQuantity), FText::AsNumber(SlotEntry.RequiredQuantity)));
		Requirement->SetFont(SWGRetailStyle::Font(9, false));
		TileContent->AddChild(Requirement);
		if (!SlotEntry.FilledObjectIds.IsEmpty())
		{
			const FSWGInventoryEntry Filled = SWGInventoryQuery::Describe(GetGameInstance(), SlotEntry.FilledObjectIds[0]);
			const FString* PendingName = SelectedResourceNames.Find(Index);
			const FString Name = Filled.Name.StartsWith(TEXT("object "))
				? (PendingName ? *PendingName : TEXT("Resource name loading")) : Filled.Name;
			UTextBlock* ResourceName = WidgetTree->ConstructWidget<UTextBlock>();
			ResourceName->SetText(FText::FromString(Name));
			ResourceName->SetFont(SWGRetailStyle::Font(9, false));
			ResourceName->SetAutoWrapText(true);
			TileContent->AddChild(ResourceName);
		}
		Grid->AddChildToUniformGrid(Tile, Index / 2, Index % 2);

		USWGTravelPlanetClickForwarder* Forwarder = NewObject<USWGTravelPlanetClickForwarder>(this);
		Forwarder->Action = [this, Index]()
		{
			const TArray<FSWGCraftingSlot>& CurrentSlots = Crafting->GetSlots();
			if (!CurrentSlots.IsValidIndex(Index))
			{
				return;
			}
			if (!CurrentSlots[Index].FilledObjectIds.IsEmpty())
			{
				// A filled slot removes its first item rather than being focused — retail lets you click a filled slot to empty it.
				Crafting->RemoveIngredient(Index, CurrentSlots[Index].FilledObjectIds[0]);
				return;
			}
			FocusedSlotIndex = Index;
			RebuildAssemblySlots();
			RebuildAssemblyCandidates();
		};
		Tile->OnClicked.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleClicked);
		ClickForwarders.Add(Forwarder);
		AssemblySlotButtons.Add(Tile);
	}
	AssembleButton->SetIsEnabled(Crafting->IsAssemblyReady());
}

void USWGCraftingWidget::RebuildAssemblyCandidates()
{
	AssemblyCandidatesList->ClearChildren();
	AssemblyCandidateButtons.Reset();
	HoveredResourceTooltip = nullptr;
	if (!Crafting || FocusedSlotIndex == INDEX_NONE)
	{
		return;
	}

	const TArray<FSWGCraftingSlot>& Slots = Crafting->GetSlots();
	const FSWGCraftingSlot* FocusedSlot = Slots.IsValidIndex(FocusedSlotIndex) ? &Slots[FocusedSlotIndex] : nullptr;
	// Resource slots filter to the required class (and its descendants) by
	// walking each candidate's held ASWGItem::ResourceType up
	// SWGResourceClass::DataTablePath's raw ENUM ancestry — the same table
	// USWGSurveySubsystem::LoadResourceTree uses, just keyed the other way
	// (from a held resource back to its class, not class name to display
	// path). Component/identical slots aren't resource-typed, so every bag
	// item is still offered for those — the server's 0x10C result is the
	// real validator either way (see GetSlotResultCaption).
	const bool bFilterByResource = FocusedSlot && FocusedSlot->Kind == ESWGDraftSlotKind::Resource && !FocusedSlot->ResourceType.IsEmpty();
	const UDataTable* ResourceClasses = bFilterByResource ? LoadObject<UDataTable>(nullptr, *SWGResourceClass::DataTablePath) : nullptr;
	USWGObjectGraphSubsystem* ObjectGraph = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;

	TArray<FSWGInventoryEntry> Equipped;
	TArray<FSWGInventoryEntry> Contents;
	SWGInventoryQuery::Gather(GetGameInstance(), Equipped, Contents);
	UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>();
	Grid->SetSlotPadding(FMargin(4.f));
	AssemblyCandidatesList->AddChild(Grid);
	int32 CandidateIndex = 0;
	for (const FSWGInventoryEntry& Entry : Contents)
	{
		if (bFilterByResource)
		{
			const ASWGItem* Item = ObjectGraph ? Cast<ASWGItem>(ObjectGraph->FindActor(Entry.ObjectId)) : nullptr;
			if (!Item || Item->ResourceType.IsEmpty()
				|| !IsResourceClassOrDescendant(ResourceClasses, Item->ResourceType, FocusedSlot->ResourceType))
			{
				continue;
			}
		}

		UVerticalBox* TileContent = nullptr;
		UButton* Tile = MakeCraftTile(WidgetTree, TileContent, false);
		USizeBox* TooltipBox = WidgetTree->ConstructWidget<USizeBox>();
		TooltipBox->SetWidthOverride(260.f);
		UTextBlock* Tooltip = WidgetTree->ConstructWidget<UTextBlock>();
		Tooltip->SetFont(SWGRetailStyle::Font(11, false));
		Tooltip->SetAutoWrapText(true);
		Tooltip->SetText(FText::FromString(Entry.Label()));
		TooltipBox->AddChild(Tooltip);
		Tile->SetToolTip(TooltipBox);
		UModelWidget* RowModel = WidgetTree->ConstructWidget<UModelWidget>();
		RowModel->DesiredSize = FVector2D(66.f, 66.f);
		RowModel->SetObject(Entry.ObjectId);
		TileContent->AddChildToVerticalBox(RowModel)->SetHorizontalAlignment(HAlign_Center);
		if (const ASWGItem* Item = ObjectGraph ? Cast<ASWGItem>(ObjectGraph->FindActor(Entry.ObjectId)) : nullptr)
		{
			if (!Item->ResourceType.IsEmpty())
			{
				FString Type = Item->ResourceType;
				Type.ReplaceInline(TEXT("_"), TEXT(" "));
				UTextBlock* TypeText = WidgetTree->ConstructWidget<UTextBlock>();
				TypeText->SetText(FText::FromString(Type));
				TypeText->SetFont(SWGRetailStyle::Font(9, false));
				TypeText->SetAutoWrapText(true);
				TileContent->AddChild(TypeText);
			}
		}
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Entry.Label()));
		Label->SetFont(SWGRetailStyle::Font(10, false));
		Label->SetAutoWrapText(true);
		TileContent->AddChild(Label);
		Grid->AddChildToUniformGrid(Tile, CandidateIndex / 2, CandidateIndex % 2);
		++CandidateIndex;

		USWGTravelPlanetClickForwarder* Forwarder = NewObject<USWGTravelPlanetClickForwarder>(this);
		const int64 ObjectId = Entry.ObjectId;
		Forwarder->Action = [this, ObjectId]()
		{
			if (Crafting && FocusedSlotIndex != INDEX_NONE)
			{
				SelectedResourceNames.Add(FocusedSlotIndex, SWGInventoryQuery::Describe(GetGameInstance(), ObjectId).Name);
				SelectedResourceSourceIds.Add(FocusedSlotIndex, ObjectId);
				Crafting->AddIngredient(ObjectId, FocusedSlotIndex);
			}
		};
		Forwarder->HoverAction = [this, ObjectId, Tooltip]()
		{
			UpdateResourceTooltip(ObjectId, Tooltip);
		};
		Tile->OnClicked.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleClicked);
		Tile->OnHovered.AddDynamic(Forwarder, &USWGTravelPlanetClickForwarder::HandleHovered);
		ClickForwarders.Add(Forwarder);
		AssemblyCandidateButtons.Add(Tile);
	}
}

void USWGCraftingWidget::UpdateResourceTooltip(int64 ObjectId, UTextBlock* Tooltip)
{
	HoveredResourceId = ObjectId;
	HoveredResourceTooltip = Tooltip;
	const FSWGInventoryEntry Entry = SWGInventoryQuery::Describe(GetGameInstance(), ObjectId);
	FString Details = Entry.Label();
	const USWGObjectGraphSubsystem* Graph = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
	if (const ASWGItem* Item = Graph ? Cast<ASWGItem>(Graph->FindActor(ObjectId)) : nullptr)
	{
		FString Type = Item->ResourceType;
		Type.ReplaceInline(TEXT("_"), TEXT(" "));
		if (!Type.IsEmpty()) { Details += TEXT("\n") + Type; }
	}
	FSWGExamineInfo Info;
	if (Examine && Examine->Describe(ObjectId, Info) && !Info.Description.IsEmpty())
	{
		Details += TEXT("\n") + Info.Description;
	}
	ResourceBaseDetails = Details;
	if (HoveredResourceTooltip) { HoveredResourceTooltip->SetText(FText::FromString(Details)); }
}

void USWGCraftingWidget::HandleResourceExamineInfo(const FSWGExamineInfo& Info)
{
	if (Info.ObjectId != HoveredResourceId || !HoveredResourceTooltip) { return; }
	FString Details = ResourceBaseDetails;
	for (const FSWGExamineAttribute& Attribute : Info.Attributes)
	{
		Details += FString::Printf(TEXT("\n%s: %s"), *Attribute.Label, *Attribute.Value);
	}
	HoveredResourceTooltip->SetText(FText::FromString(Details));
}

void USWGCraftingWidget::HandleSessionStarted()
{
	SelectedTab = 0;
	HighlightedIndex = INDEX_NONE;
	FocusedSlotIndex = INDEX_NONE;
	HoveredResourceId = 0;
	SelectedResourceNames.Reset();
	SelectedResourceSourceIds.Reset();
	CollapsedSchematicNodes.Reset();
	ExperimentAllocations.Reset();
	bExperimentPending = false;
	CustomizeVarValues.Reset();
	SelectedTemplateChoice = INDEX_NONE;
	TransientStatus = FText::GetEmpty();
	RebuildTabs();
	RebuildSchematicList();
	RefreshDetails();
	RebuildAssemblySlots();
	RebuildAssemblyCandidates();
	RebuildExperimentRows();
	RebuildCustomizePage();
	RefreshSummaryPage();
	RefreshPageVisibility();
	RefreshStatus();
}

void USWGCraftingWidget::HandleStageChanged()
{
	TransientStatus = FText::GetEmpty();
	if (Crafting && Crafting->GetState() == ESWGCraftingSessionState::Experimenting)
	{
		ExperimentAllocations.Init(0, Crafting->GetExperimentGroups().Num());
		bExperimentPending = false;
	}
	RebuildExperimentRows();
	RefreshPageVisibility();
	RefreshStatus();
}

void USWGCraftingWidget::HandleSlotsChanged()
{
	RebuildAssemblySlots();
	RebuildAssemblyCandidates();
	RebuildExperimentRows();
	RebuildCustomizePage();
	RefreshSummaryPage();
	RefreshPrototypeModel();
	RefreshStatus();
}

void USWGCraftingWidget::HandleSlotResult(ESWGCraftingSlotResult Result)
{
	TransientStatus = GetSlotResultCaption(Result);
	RefreshStatus();
}

void USWGCraftingWidget::HandleAssemblyResult(ESWGCraftingResult Result)
{
	TransientStatus = GetAssemblyResultCaption(Result);
	RefreshStatus();
}

void USWGCraftingWidget::HandleExperimentResult(ESWGCraftingResult Result)
{
	bExperimentPending = false;
	ExperimentAllocations.Init(0, Crafting->GetExperimentGroups().Num());
	TransientStatus = GetAssemblyResultCaption(Result);
	RebuildExperimentRows();
	RefreshStatus();
}

void USWGCraftingWidget::HandleSessionClosed()
{
	Close();
}

void USWGCraftingWidget::HandleDraftPreviewChanged()
{
	RefreshDetails();
}

void USWGCraftingWidget::HandleSelectClicked()
{
	if (Crafting && Crafting->GetSchematics().IsValidIndex(HighlightedIndex))
	{
		Crafting->SelectSchematic(HighlightedIndex);
	}
}

void USWGCraftingWidget::HandleAssembleClicked()
{
	if (Crafting)
	{
		Crafting->Assemble();
	}
}

void USWGCraftingWidget::HandleExperimentClicked()
{
	if (!Crafting || bExperimentPending)
	{
		return;
	}
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
	if (Crafting->Experiment(Rows))
	{
		bExperimentPending = true;
		RebuildExperimentRows();
	}
}

void USWGCraftingWidget::HandleExperimentContinueClicked()
{
	if (Crafting) { Crafting->Assemble(); }
}

void USWGCraftingWidget::HandleCustomizeApplyClicked()
{
	if (!Crafting)
	{
		return;
	}
	TArray<FSWGCraftingCustomizationEdit> Edits;
	for (int32 Index = 0; Index < CustomizeVarValues.Num(); ++Index)
	{
		FSWGCraftingCustomizationEdit& Edit = Edits.AddDefaulted_GetRef();
		Edit.Index = Index;
		Edit.Value = CustomizeVarValues[Index];
	}
	const uint8 TemplateChoice = SelectedTemplateChoice != INDEX_NONE ? static_cast<uint8>(SelectedTemplateChoice) : 0xFF;
	Crafting->Customize(CustomizeNameBox ? CustomizeNameBox->GetText().ToString() : FString(), TemplateChoice, 1, Edits);
}

void USWGCraftingWidget::HandleCustomizeContinueClicked()
{
	if (Crafting) { Crafting->Assemble(); }
}

void USWGCraftingWidget::HandleSummaryPracticeClicked()
{
	if (Crafting) { Crafting->CreatePrototype(true); }
}

void USWGCraftingWidget::HandleSummaryCreateClicked()
{
	if (Crafting) { Crafting->CreatePrototype(false); }
}

void USWGCraftingWidget::HandleSummarySchematicClicked()
{
	if (Crafting) { Crafting->CreateManufactureSchematic(); }
}

void USWGCraftingWidget::HandleSummaryRetrieveClicked()
{
	if (Crafting) { Crafting->RetrieveOutput(Crafting->GetToolObjectId()); }
}

static FAutoConsoleCommand GCraftDumpAncestryCommand(
	TEXT("swg.Craft.DumpAncestry"),
	TEXT("swg.Craft.DumpAncestry <resourceClassId> — walks SWGResourceClass::DataTablePath's ParentClass chain from the given id and logs it, to sanity-check resource-filter matching."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("usage: swg.Craft.DumpAncestry <resourceClassId>"));
			return;
		}
		const UDataTable* Table = LoadObject<UDataTable>(nullptr, *SWGResourceClass::DataTablePath);
		if (!Table)
		{
			UE_LOG(LogTemp, Warning, TEXT("swg.Craft.DumpAncestry: could not load %s"), *SWGResourceClass::DataTablePath);
			return;
		}
		FString Current = Args[0];
		FString Chain = Current;
		for (int32 Depth = 0; Depth < 16; ++Depth)
		{
			const FSWGResourceClassRow* Row = Table->FindRow<FSWGResourceClassRow>(FName(*Current), TEXT("Craft"), false);
			if (!Row || Row->ParentClass.IsEmpty())
			{
				break;
			}
			Chain += TEXT(" -> ") + Row->ParentClass;
			Current = Row->ParentClass;
		}
		UE_LOG(LogTemp, Log, TEXT("swg.Craft.DumpAncestry: %s"), *Chain);
	}));

static FAutoConsoleCommand GCraftDumpInventoryCommand(
	TEXT("swg.Craft.DumpInventory"),
	TEXT("Logs the local player's bag contents (object id, name, quantity) to LogTemp, for picking an id to test swg.Craft.Add with."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		UGameInstance* GameInstance = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
			{
				GameInstance = Context.World()->GetGameInstance();
				break;
			}
		}
		USWGObjectGraphSubsystem* ObjectGraph = GameInstance ? GameInstance->GetSubsystem<USWGObjectGraphSubsystem>() : nullptr;
		TArray<FSWGInventoryEntry> Equipped;
		TArray<FSWGInventoryEntry> Contents;
		SWGInventoryQuery::Gather(GameInstance, Equipped, Contents);
		UE_LOG(LogTemp, Log, TEXT("swg.Craft.DumpInventory: %d item(s) in bag"), Contents.Num());
		for (const FSWGInventoryEntry& Entry : Contents)
		{
			const ASWGItem* Item = ObjectGraph ? Cast<ASWGItem>(ObjectGraph->FindActor(Entry.ObjectId)) : nullptr;
			UE_LOG(LogTemp, Log, TEXT("  id=%lld name=%s qty=%d resourceType=%s"), Entry.ObjectId, *Entry.Name, Entry.Quantity,
				Item ? *Item->ResourceType : TEXT("(no actor)"));
		}
	}));
