#include "SWGCoreUISubsystem.h"
#include "SWGCoreUISettings.h"
#include "SWGUISubsystem.h"
#include "SWGGameLayout.h"
#include "SWGStateTransitionConfig.h"
#include "SWGRadialMenuWidget.h"
#include "SWGSuiBoxWidget.h"
#include "Subsystems/SWGClientFlowSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

void USWGCoreUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The router first, so it exists to register with.
	if (USWGUISubsystem* Router = Collection.InitializeDependency<USWGUISubsystem>())
	{
		Router->RegisterPresenter(*this);
	}

	UGameInstance* GameInstance = GetLocalPlayer()->GetGameInstance();
	if (USWGClientFlowSubsystem* Flow = GameInstance->GetSubsystem<USWGClientFlowSubsystem>())
	{
		Flow->OnStateChanged.AddDynamic(this, &USWGCoreUISubsystem::HandleStateChanged);
	}
	if (USWGRadialMenuSubsystem* Radial = GameInstance->GetSubsystem<USWGRadialMenuSubsystem>())
	{
		Radial->OnMenuReceived.AddDynamic(this, &USWGCoreUISubsystem::HandleRadialMenuReceived);
	}
	if (USWGSuiSubsystem* Sui = GameInstance->GetSubsystem<USWGSuiSubsystem>())
	{
		Sui->OnPageOpened.AddDynamic(this, &USWGCoreUISubsystem::HandleSuiPageOpened);
		Sui->OnPageClosed.AddDynamic(this, &USWGCoreUISubsystem::HandleSuiPageClosed);
	}
}

void USWGCoreUISubsystem::Deinitialize()
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
	if (USWGUISubsystem* Router = GetRouter())
	{
		Router->UnregisterPresenter(*this);
	}
	Super::Deinitialize();
}

USWGUISubsystem* USWGCoreUISubsystem::GetRouter() const
{
	return GetLocalPlayer() ? GetLocalPlayer()->GetSubsystem<USWGUISubsystem>() : nullptr;
}

// ── Hud feature ──────────────────────────────────────────────────────────────

void USWGCoreUISubsystem::OpenFeature(ESWGUIFeature Feature, const FSWGUIFeatureRequest& Request)
{
	if (Feature != ESWGUIFeature::Hud)
	{
		return;
	}
	TSubclassOf<UCommonActivatableWidget> HudClass = USWGCoreUISettings::Get().HudClass.LoadSynchronous();
	USWGGameLayout* Layout = EnsureLayout();
	if (!Layout || !HudClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGCoreUISubsystem: cannot open the HUD — %s"),
			Layout ? TEXT("no HudClass set in Project Settings > SWG UI (Core)") : TEXT("no layout"));
		return;
	}
	Layout->SetHudWidget(HudClass);
}

void USWGCoreUISubsystem::CloseFeature(ESWGUIFeature Feature)
{
	if (USWGGameLayout* Layout = Feature == ESWGUIFeature::Hud ? USWGGameLayout::GetLayout(GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr) : nullptr)
	{
		Layout->ClearLayer(USWGGameLayout::TAG_Layer_Hud);
	}
}

bool USWGCoreUISubsystem::IsFeatureOpen(ESWGUIFeature Feature) const
{
	const USWGGameLayout* Layout = USWGGameLayout::GetLayout(GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr);
	return Feature == ESWGUIFeature::Hud && Layout && Layout->IsHudShown();
}

float USWGCoreUISubsystem::GetHudOpacity() const
{
	const USWGGameLayout* Layout = USWGGameLayout::GetLayout(GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr);
	return Layout ? Layout->GetRenderOpacity() : 1.f;
}

void USWGCoreUISubsystem::SetHudOpacity(float Opacity)
{
	if (USWGGameLayout* Layout = USWGGameLayout::GetLayout(GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr))
	{
		Layout->SetRenderOpacity(Opacity);
	}
}

// ── Radial menu and SUI ──────────────────────────────────────────────────────

void USWGCoreUISubsystem::HandleRadialMenuReceived(const FSWGRadialMenu& Menu)
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

	TSubclassOf<USWGRadialMenuWidget> MenuClass = USWGCoreUISettings::Get().RadialMenuClass.LoadSynchronous();
	if (!MenuClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGCoreUISubsystem: no RadialMenuClass set in Project Settings > SWG UI (Core)"));
		return;
	}

	RadialMenu = CreateWidget<USWGRadialMenuWidget>(PlayerController, MenuClass);
	if (RadialMenu)
	{
		// Above the layout (which sits at 100) so it draws over the HUD.
		RadialMenu->AddToPlayerScreen(200);
		RadialMenu->OnClosed.AddUObject(this, &USWGCoreUISubsystem::HandleRadialMenuClosed);
		RadialMenu->Open(Menu);
	}
	UE_LOG(LogTemp, Log, TEXT("USWGCoreUISubsystem: radial menu for %lld at (%.0f, %.0f) -> %s"),
		Menu.ObjectId, Menu.ScreenPosition.X, Menu.ScreenPosition.Y, RadialMenu ? *RadialMenu->GetName() : TEXT("FAILED"));
}

void USWGCoreUISubsystem::HandleRadialMenuClosed()
{
	if (USWGUISubsystem* Router = GetRouter())
	{
		Router->NotifyRadialMenuClosed();
	}
}

void USWGCoreUISubsystem::HandleSuiPageOpened(const FSWGSuiPage& Page)
{
	USWGGameLayout* Layout = EnsureLayout();
	TSubclassOf<USWGSuiBoxWidget> BoxClass = USWGCoreUISettings::Get().SuiBoxClass.LoadSynchronous();
	if (!Layout || !BoxClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("USWGCoreUISubsystem: SUI page %d (%s) dropped — %s"), Page.PageId, *Page.ScriptClass,
			Layout ? TEXT("no SuiBoxClass set in Project Settings > SWG UI (Core)") : TEXT("no layout"));
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

void USWGCoreUISubsystem::HandleSuiPageClosed(int32 PageId)
{
	TObjectPtr<USWGSuiBoxWidget> Window;
	if (SuiWindows.RemoveAndCopyValue(PageId, Window) && Window && Window->IsActivated())
	{
		Window->DeactivateWidget();
	}
}

// ── Layout and client flow ───────────────────────────────────────────────────

void USWGCoreUISubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);

	// Runs from SetPlayer() during SpawnPlayActor, so the layout exists before
	// the controller's BeginPlay kicks off the flow's first transition.
	if (NewPlayerController)
	{
		EnsureLayout();
	}
}

USWGGameLayout* USWGCoreUISubsystem::EnsureLayout()
{
	APlayerController* PlayerController = GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(nullptr) : nullptr;
	if (!PlayerController)
	{
		return USWGGameLayout::GetLayout(nullptr);
	}

	TSubclassOf<USWGGameLayout> LayoutClass = USWGCoreUISettings::Get().LayoutClass.LoadSynchronous();
	if (!LayoutClass)
	{
		UE_LOG(LogTemp, Error, TEXT("USWGCoreUISubsystem: no LayoutClass set in Project Settings > SWG UI (Core)"));
		return nullptr;
	}

	bool bCreated = false;
	return USWGGameLayout::GetOrCreate(PlayerController, LayoutClass, bCreated);
}

void USWGCoreUISubsystem::HandleStateChanged(ESWGClientState OldState, ESWGClientState NewState)
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

	if (const UDataTable* Table = USWGCoreUISettings::Get().StateTransitionTable.LoadSynchronous())
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
