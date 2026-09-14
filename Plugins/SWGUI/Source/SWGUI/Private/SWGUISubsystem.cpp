#include "SWGUISubsystem.h"
#include "SWGUISettings.h"
#include "SWGGameLayout.h"
#include "SWGStateTransitionConfig.h"
#include "SWGRadialMenuWidget.h"
#include "SWGSuiBoxWidget.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"

void USWGUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UGameInstance* GameInstance = GetLocalPlayer()->GetGameInstance();
	if (USWGClientFlowSubsystem* Flow = GameInstance->GetSubsystem<USWGClientFlowSubsystem>())
	{
		Flow->OnStateChanged.AddDynamic(this, &USWGUISubsystem::HandleStateChanged);
	}
	if (USWGRadialMenuSubsystem* Radial = GameInstance->GetSubsystem<USWGRadialMenuSubsystem>())
	{
		Radial->OnMenuReceived.AddDynamic(this, &USWGUISubsystem::HandleRadialMenuReceived);
	}
	if (USWGSuiSubsystem* Sui = GameInstance->GetSubsystem<USWGSuiSubsystem>())
	{
		Sui->OnPageOpened.AddDynamic(this, &USWGUISubsystem::HandleSuiPageOpened);
		Sui->OnPageClosed.AddDynamic(this, &USWGUISubsystem::HandleSuiPageClosed);
	}
}

void USWGUISubsystem::Deinitialize()
{
	if (UGameInstance* GameInstance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr)
	{
		if (USWGClientFlowSubsystem* Flow = GameInstance->GetSubsystem<USWGClientFlowSubsystem>())
		{
			Flow->OnStateChanged.RemoveAll(this);
		}
		if (USWGRadialMenuSubsystem* Radial = GameInstance->GetSubsystem<USWGRadialMenuSubsystem>())
		{
			Radial->OnMenuReceived.RemoveAll(this);
		}
		if (USWGSuiSubsystem* Sui = GameInstance->GetSubsystem<USWGSuiSubsystem>())
		{
			Sui->OnPageOpened.RemoveAll(this);
			Sui->OnPageClosed.RemoveAll(this);
		}
	}

	Super::Deinitialize();
}

void USWGUISubsystem::HandleRadialMenuReceived(const FSWGRadialMenu& Menu)
{
	APlayerController* PlayerController = GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
	if (!PlayerController || Menu.Items.IsEmpty())
	{
		return;
	}

	if (RadialMenu)
	{
		RadialMenu->Close();
		RadialMenu = nullptr;
	}

	TSubclassOf<USWGRadialMenuWidget> MenuClass = USWGUISettings::Get().RadialMenuClass.LoadSynchronous();
	if (!MenuClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: no RadialMenuClass set in Project Settings > SWG UI"));
		return;
	}

	RadialMenu = CreateWidget<USWGRadialMenuWidget>(PlayerController, MenuClass);
	if (RadialMenu)
	{
		// Above the layout (which sits at 100) so it draws over the HUD.
		RadialMenu->AddToPlayerScreen(200);
		RadialMenu->Open(Menu);
	}
	UE_LOG(LogTemp, Log, TEXT("USWGUISubsystem: radial menu for %lld at (%.0f, %.0f) -> %s"),
		Menu.ObjectId, Menu.ScreenPosition.X, Menu.ScreenPosition.Y, RadialMenu ? *RadialMenu->GetName() : TEXT("FAILED"));
}

void USWGUISubsystem::HandleSuiPageOpened(const FSWGSuiPage& Page)
{
	USWGGameLayout* Layout = EnsureLayout();
	TSubclassOf<USWGSuiBoxWidget> BoxClass = USWGUISettings::Get().SuiBoxClass.LoadSynchronous();
	if (!Layout || !BoxClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGUISubsystem: SUI page %d (%s) dropped — %s"), Page.PageId, *Page.ScriptClass,
			Layout ? TEXT("no SuiBoxClass set in Project Settings > SWG UI") : TEXT("no layout"));
		return;
	}

	// An update to an open page re-fills its window in place.
	if (TObjectPtr<USWGSuiBoxWidget>* Existing = SuiWindows.Find(Page.PageId); Existing && *Existing)
	{
		(*Existing)->SetPage(Page);
		return;
	}

	USWGSuiBoxWidget* Window = Cast<USWGSuiBoxWidget>(Layout->PushWidgetToLayerStack(USWGGameLayout::TAG_Layer_Modal, BoxClass));
	if (Window)
	{
		Window->SetPage(Page);
		SuiWindows.Add(Page.PageId, Window);
	}
}

void USWGUISubsystem::HandleSuiPageClosed(int32 PageId)
{
	TObjectPtr<USWGSuiBoxWidget> Window;
	if (SuiWindows.RemoveAndCopyValue(PageId, Window) && Window && Window->IsActivated())
	{
		Window->DeactivateWidget();
	}
}

void USWGUISubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);

	// Runs from SetPlayer() during SpawnPlayActor, so the layout exists before
	// the controller's BeginPlay kicks off the flow's first transition.
	if (NewPlayerController)
	{
		EnsureLayout();
	}
}

USWGGameLayout* USWGUISubsystem::EnsureLayout()
{
	APlayerController* PlayerController = GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
	if (!PlayerController)
	{
		return USWGGameLayout::GetLayout(nullptr);
	}

	TSubclassOf<USWGGameLayout> LayoutClass = USWGUISettings::Get().LayoutClass.LoadSynchronous();
	if (!LayoutClass)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGUISubsystem: no LayoutClass set in Project Settings > SWG UI"));
		return nullptr;
	}

	bool bCreated = false;
	return USWGGameLayout::GetOrCreate(PlayerController, LayoutClass, bCreated);
}

void USWGUISubsystem::HandleStateChanged(ESWGClientState OldState, ESWGClientState NewState)
{
	USWGGameLayout* Layout = EnsureLayout();
	if (!Layout)
	{
		return;
	}

	// A scene change from in world (teleport, zone travel) wants the same
	// loading screen as the one from character select.
	const ESWGClientState RowOldState = (OldState == ESWGClientState::InWorld && NewState == ESWGClientState::ZoneLoading)
		? ESWGClientState::CharacterSelected : OldState;

	if (const UDataTable* Table = USWGUISettings::Get().StateTransitionTable.LoadSynchronous())
	{
		for (const auto& Row : Table->GetRowMap())
		{
			const FSWGStateTransitionRow* TransitionRow = reinterpret_cast<const FSWGStateTransitionRow*>(Row.Value);
			if (TransitionRow && TransitionRow->OldState == RowOldState && TransitionRow->NewState == NewState)
			{
				const FGameplayTag Tag = TransitionRow->LayerTag.IsValid() ? TransitionRow->LayerTag : USWGGameLayout::TAG_Layer_Menu;
				Layout->PushWidgetToLayerStack(Tag, TransitionRow->WidgetClass);
				break;
			}
		}
	}

	if (NewState == ESWGClientState::CharacterSelected)
	{
		Layout->ClearLayer(USWGGameLayout::TAG_Layer_Menu);
	}
	else if (NewState == ESWGClientState::InWorld)
	{
		Layout->ClearLayer(USWGGameLayout::TAG_Layer_Menu);
		Layout->ClearLayer(USWGGameLayout::TAG_Layer_Loading);
		Layout->ClearLayer(USWGGameLayout::TAG_Layer_Modal);
	}
}
